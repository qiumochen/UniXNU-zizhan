/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_SLPI_STD_H
#define ZIZHAN_SLPI_STD_H
#include <stdint.h>
#include <string.h>
/*
 * SENS-02: the "apps_std" module a FastRPC daemon offers its DSP. The sensor PD of the SLPI reads its registry inputs through
 * it (sns_reg_config, the /sys/devices/soc0 identity files, the sensor JSON configs) and writes the parsed registry back, as
 * sscrpcd serves them on Android. Here the files live in memory: a read-only seed archive handed over at boot plus whatever the
 * DSP writes during the session.
 *
 * Wire layout of every method (argument order, which lengths sit in the primary buffers) as generated from
 * qualcomm/fastrpc idl/apps_std.idl (BSD-3-Clause); results are success 0 or the errno the reference library returns. No kernel
 * dependency: the host test tools/zizhan/tests/slpi-std.cpp drives the same code.
 *
 * Seed archive: "ZSF1", u32 count, then per entry u16 path length, u16 0, u32 data length, the absolute path, the data.
 */
namespace zizhan_slpi_std {
enum : int32_t { kOk = 0, kNoEnt = 2, kBadF = 9, kNoMem = 12, kExist = 17, kNotDir = 20, kIsDir = 21, kInval = 22, kMFile = 24,
                 kNoSpc = 28, kNameTooLong = 36, kNotEmpty = 39, kUnsupported = 0x14, kGetEnv = 0x63 };
constexpr unsigned kNodes = 1024, kFiles = 32, kDirs = 8, kPath = 160;
struct Node { char path[kPath]; uint8_t *data; uint32_t size, cap; uint8_t used, dir, seed; };
struct File { int32_t node; uint32_t pos; uint8_t used, write, append; };
struct Dir { char path[kPath]; uint32_t next; uint8_t used; };
struct Fs {
  Node nodes[kNodes];
  File files[kFiles];
  Dir dirs[kDirs];
  void *(*alloc)(uint32_t);
  void (*release)(void *, uint32_t);
  void (*print)(const char *);
  uint32_t ramBytes, ramLimit;
  uint32_t opens, missing, readBytes, writtenBytes, calls;
  char lastMissing[kPath];
};

inline void copyText(char *to, const char *from, unsigned cap) {
  unsigned n = 0;
  for (; n + 1 < cap && from[n]; ++n) { to[n] = from[n]; }
  to[n] = 0;
}
/* Absolute path without repeated or trailing slashes, "." and ".." resolved (the registry writes ".../registry/../temp.json"). */
inline bool normalize(const char *in, char *out) {
  if (!in || in[0] != '/') { return false; }
  unsigned n = 0;
  const char *p = in;
  while (*p) {
    while (*p == '/') { ++p; }
    const char *start = p;
    while (*p && *p != '/') { ++p; }
    const unsigned len = unsigned(p - start);
    if (!len || (len == 1 && start[0] == '.')) { continue; }
    if (len == 2 && start[0] == '.' && start[1] == '.') {
      while (n && out[n - 1] != '/') { --n; }
      if (n) { --n; }
      continue;
    }
    if (n + 1 + len >= kPath) { return false; }
    out[n++] = '/';
    memcpy(out + n, start, len);
    n += len;
  }
  if (!n) { out[n++] = '/'; }
  out[n] = 0;
  return true;
}
inline int find(const Fs &fs, const char *path) {
  for (unsigned i = 0; i < kNodes; ++i) {
    if (fs.nodes[i].used && !strcmp(fs.nodes[i].path, path)) { return int(i); }
  }
  return -1;
}
inline void parentOf(const char *path, char *out) {
  unsigned last = 0;
  for (unsigned i = 0; path[i]; ++i) {
    if (path[i] == '/') { last = i; }
  }
  if (!last) { out[0] = '/'; out[1] = 0; return; }
  memcpy(out, path, last);
  out[last] = 0;
}
inline int add(Fs &fs, const char *path, bool dir) {
  /* missing parents become directories (the registry creates its output tree level by level anyway) */
  char parent[kPath];
  parentOf(path, parent);
  if (strcmp(parent, path) && find(fs, parent) < 0 && add(fs, parent, true) < 0) { return -1; }
  for (unsigned i = 0; i < kNodes; ++i) {
    Node &n = fs.nodes[i];
    if (n.used) { continue; }
    memset(&n, 0, sizeof n);
    copyText(n.path, path, kPath);
    n.used = 1;
    n.dir = dir ? 1 : 0;
    return int(i);
  }
  return -1;
}
inline void drop(Fs &fs, Node &n) {
  if (n.data && !n.seed) { fs.release(n.data, n.cap); fs.ramBytes -= n.cap; }
  memset(&n, 0, sizeof n);
}
/* Make room for size bytes of the session's own data (a seed file is copied first). */
inline bool reserve(Fs &fs, Node &n, uint32_t size) {
  if (!n.seed && size <= n.cap) { return true; }
  uint32_t cap = n.seed || !n.cap ? 1024 : n.cap;
  if (size < n.size) { size = n.size; }
  while (cap < size) {
    if (cap > (64u << 20)) { return false; }
    cap *= 2;
  }
  if (fs.ramBytes + cap > fs.ramLimit) { return false; }
  uint8_t *data = static_cast<uint8_t *>(fs.alloc(cap));
  if (!data) { return false; }
  if (n.size) { memcpy(data, n.data, n.size); }
  if (n.data && !n.seed) { fs.release(n.data, n.cap); fs.ramBytes -= n.cap; }
  n.data = data; n.cap = cap; n.seed = 0;
  fs.ramBytes += cap;
  return true;
}
inline bool mountSeed(Fs &fs, const uint8_t *blob, uint32_t bytes, unsigned *files) {
  uint32_t count = 0, at = 8;
  if (bytes < 8 || memcmp(blob, "ZSF1", 4)) { return false; }
  memcpy(&count, blob + 4, 4);
  if (find(fs, "/") < 0) { add(fs, "/", true); }
  for (uint32_t i = 0; i < count; ++i) {
    uint16_t plen = 0;
    uint32_t dlen = 0;
    char raw[kPath], path[kPath];
    if (bytes - at < 8) { return false; }
    memcpy(&plen, blob + at, 2);
    memcpy(&dlen, blob + at + 4, 4);
    at += 8;
    if (!plen || plen >= kPath || bytes - at < plen || bytes - at - plen < dlen) { return false; }
    memcpy(raw, blob + at, plen);
    raw[plen] = 0;
    at += plen;
    if (!normalize(raw, path)) { return false; }
    int node = find(fs, path);
    if (node < 0) { node = add(fs, path, false); }
    if (node < 0 || fs.nodes[node].dir) { return false; }
    fs.nodes[node].data = const_cast<uint8_t *>(blob + at);
    fs.nodes[node].size = dlen;
    fs.nodes[node].seed = 1;
    at += dlen;
    if (files) { ++*files; }
  }
  return true;
}

struct Call {
  const uint8_t *const *in; const uint32_t *inLen; unsigned nin;
  uint8_t *const *out; const uint32_t *outLen; unsigned nout;
  uint32_t word(unsigned i) const {   /* word i of the primary in buffer */
    uint32_t v = 0;
    if (nin && in[0] && inLen[0] >= 4 * i + 4) { memcpy(&v, in[0] + 4 * i, 4); }
    return v;
  }
  const char *str(unsigned i) const {   /* in buffer i as a terminated string */
    if (i >= nin || !in[i] || !inLen[i] || in[i][inLen[i] - 1]) { return nullptr; }
    return reinterpret_cast<const char *>(in[i]);
  }
  bool room(unsigned o, uint32_t bytes) const { return o < nout && out[o] && outLen[o] >= bytes; }
  void put(unsigned o, uint32_t at, const void *v, uint32_t n) const {
    if (room(o, at + n)) { memcpy(out[o] + at, v, n); }
  }
  void put32(unsigned o, unsigned i, uint32_t v) const { put(o, 4 * i, &v, 4); }
};
inline File *fileOf(Fs &fs, uint32_t handle) {
  if (!handle || handle > kFiles || !fs.files[handle - 1].used) { return nullptr; }
  return &fs.files[handle - 1];
}
inline int32_t open(Fs &fs, const char *name, const char *mode, uint32_t *handle) {
  char path[kPath];
  if (!name || !mode) { return kInval; }
  if (!normalize(name, path)) { return name[0] == '/' ? kNameTooLong : kNoEnt; }
  const bool plus = strchr(mode, '+') != nullptr, w = mode[0] == 'w', a = mode[0] == 'a';
  if (mode[0] != 'r' && !w && !a) { return kInval; }
  int node = find(fs, path);
  if (node >= 0 && fs.nodes[node].dir) { return kIsDir; }
  if (node < 0) {
    if (!w && !a) {
      ++fs.missing;
      copyText(fs.lastMissing, path, kPath);
      return kNoEnt;
    }
    node = add(fs, path, false);
    if (node < 0) { return kNoSpc; }
  } else if (w) {
    Node &n = fs.nodes[node];
    if (n.seed) { n.data = nullptr; n.cap = 0; n.seed = 0; }
    n.size = 0;
  }
  for (unsigned i = 0; i < kFiles; ++i) {
    File &f = fs.files[i];
    if (f.used) { continue; }
    f = {node, 0, 1, uint8_t(w || a || plus), uint8_t(a)};
    *handle = i + 1;
    ++fs.opens;
    return kOk;
  }
  return kMFile;
}
inline int32_t write(Fs &fs, File &f, const uint8_t *data, uint32_t len, uint32_t *done) {
  Node &n = fs.nodes[f.node];
  *done = 0;
  if (!f.write) { return kBadF; }
  if (f.append) { f.pos = n.size; }
  if (len > 0xffffffffu - f.pos) { return kInval; }
  if (!reserve(fs, n, f.pos + len)) { return kNoSpc; }
  if (f.pos > n.size) { memset(n.data + n.size, 0, f.pos - n.size); }
  if (len) { memcpy(n.data + f.pos, data, len); }
  f.pos += len;
  if (f.pos > n.size) { n.size = f.pos; }
  *done = len;
  fs.writtenBytes += len;
  return kOk;
}
inline bool hasChildren(const Fs &fs, const char *dir) {
  char parent[kPath];
  for (unsigned i = 0; i < kNodes; ++i) {
    if (!fs.nodes[i].used || !strcmp(fs.nodes[i].path, dir)) { continue; }
    parentOf(fs.nodes[i].path, parent);
    if (!strcmp(parent, dir)) { return true; }
  }
  return false;
}
/* One apps_std call. method is the real method number (the caller resolves the extended form 31+). */
inline int32_t invoke(Fs &fs, uint32_t method, const Call &c) {
  char path[kPath], other[kPath];
  uint32_t handle = 0;
  ++fs.calls;
  switch (method) {
  case 0: {    /* fopen(name, mode) -> FILE */
    const int32_t r = open(fs, c.str(1), c.str(2), &handle);
    c.put32(0, 0, handle);
    return r;
  }
  case 19: {   /* fopen_with_env(env, delim, name, mode) -> FILE: only absolute names exist here */
    const int32_t r = open(fs, c.str(3), c.str(4), &handle);
    c.put32(0, 0, handle);
    return r;
  }
  case 2: case 14: case 23:   /* fflush, clearerr, fsync */
    return fileOf(fs, c.word(0)) ? kOk : kBadF;
  case 11: {   /* rewind */
    File *f = fileOf(fs, c.word(0));
    if (!f) { return kBadF; }
    f->pos = 0;
    return kOk;
  }
  case 3: {    /* fclose */
    File *f = fileOf(fs, c.word(0));
    if (!f) { return kBadF; }
    f->used = 0;
    return kOk;
  }
  case 4: {    /* fread(FILE, len) -> bytes, eof + data */
    File *f = fileOf(fs, c.word(0));
    if (!f) { return kBadF; }
    const Node &n = fs.nodes[f->node];
    uint32_t want = c.word(1);
    if (!c.room(1, want)) { want = c.nout > 1 ? c.outLen[1] : 0; }
    const uint32_t left = f->pos < n.size ? n.size - f->pos : 0, got = want < left ? want : left;
    if (got) { memcpy(c.out[1], n.data + f->pos, got); }
    f->pos += got;
    fs.readBytes += got;
    c.put32(0, 0, got);
    c.put32(0, 1, got < want ? 1 : 0);
    return kOk;
  }
  case 20: {   /* fgets(FILE, len) -> eof + line */
    File *f = fileOf(fs, c.word(0));
    if (!f) { return kBadF; }
    const Node &n = fs.nodes[f->node];
    uint32_t cap = c.word(1), got = 0;
    if (!c.room(1, cap)) { cap = c.nout > 1 ? c.outLen[1] : 0; }
    while (cap && got + 1 < cap && f->pos < n.size) {
      const uint8_t ch = n.data[f->pos++];
      c.out[1][got++] = ch;
      if (ch == '\n') { break; }
    }
    if (cap) { c.out[1][got] = 0; }
    fs.readBytes += got;
    c.put32(0, 0, got ? 0 : 1);
    return kOk;
  }
  case 5: {    /* fwrite(FILE, len, data) -> bytes, eof */
    File *f = fileOf(fs, c.word(0));
    uint32_t len = c.word(1), done = 0;
    if (!f) { return kBadF; }
    if (c.nin < 2 || (len && !c.in[1]) || c.inLen[1] < len) { return kInval; }
    const int32_t r = write(fs, *f, c.in[1], len, &done);
    c.put32(0, 0, done);
    c.put32(0, 1, 0);
    return r;
  }
  case 8: {    /* ftell */
    File *f = fileOf(fs, c.word(0));
    if (!f) { return kBadF; }
    c.put32(0, 0, f->pos);
    return kOk;
  }
  case 9: {    /* fseek(FILE, offset, whence) */
    File *f = fileOf(fs, c.word(0));
    if (!f) { return kBadF; }
    const int64_t base = c.word(2) == 0 ? 0 : c.word(2) == 1 ? int64_t(f->pos) : c.word(2) == 2 ? int64_t(fs.nodes[f->node].size) : -1;
    const int64_t to = base + int32_t(c.word(1));
    if (base < 0 || to < 0 || to > 0x7fffffff) { return kInval; }
    f->pos = uint32_t(to);
    return kOk;
  }
  case 10: {   /* flen -> uint64 */
    File *f = fileOf(fs, c.word(0));
    if (!f) { return kBadF; }
    const uint64_t len = fs.nodes[f->node].size;
    c.put(0, 0, &len, 8);
    return kOk;
  }
  case 12: {   /* feof */
    File *f = fileOf(fs, c.word(0));
    if (!f) { return kBadF; }
    c.put32(0, 0, f->pos >= fs.nodes[f->node].size ? 1 : 0);
    return kOk;
  }
  case 13:     /* ferror */
    if (!fileOf(fs, c.word(0))) { return kBadF; }
    c.put32(0, 0, 0);
    return kOk;
  case 15:     /* print_string */
    if (fs.print && c.str(1)) { fs.print(c.str(1)); }
    return kOk;
  case 16:     /* getenv: nothing is set */
    c.put32(0, 0, 0);
    return kGetEnv;
  case 22: {   /* fileExists -> bool */
    const uint8_t yes = c.str(1) && normalize(c.str(1), path) && find(fs, path) >= 0 ? 1 : 0;
    c.put(0, 0, &yes, 1);
    return kOk;
  }
  case 24: {   /* fremove */
    if (!c.str(1) || !normalize(c.str(1), path)) { return kNoEnt; }
    const int node = find(fs, path);
    if (node < 0) { return kNoEnt; }
    if (fs.nodes[node].dir) { return kIsDir; }
    for (unsigned i = 0; i < kFiles; ++i) {
      if (fs.files[i].used && fs.files[i].node == node) { fs.files[i].used = 0; }
    }
    drop(fs, fs.nodes[node]);
    return kOk;
  }
  case 26: {   /* opendir -> uint64 handle */
    if (!c.str(1) || !normalize(c.str(1), path)) { return kNoEnt; }
    const int node = find(fs, path);
    if (node < 0) { return kNoEnt; }
    if (!fs.nodes[node].dir) { return kNotDir; }
    for (unsigned i = 0; i < kDirs; ++i) {
      Dir &d = fs.dirs[i];
      if (d.used) { continue; }
      copyText(d.path, path, kPath);
      d.next = 0;
      d.used = 1;
      const uint64_t h = i + 1;
      c.put(0, 0, &h, 8);
      return kOk;
    }
    return kMFile;
  }
  case 27: case 28: {   /* closedir, readdir(handle) -> {ino, name[255]}, eof */
    uint64_t h = 0;
    if (c.nin && c.in[0] && c.inLen[0] >= 8) { memcpy(&h, c.in[0], 8); }
    if (!h || h > kDirs || !fs.dirs[h - 1].used) { return kBadF; }
    Dir &d = fs.dirs[h - 1];
    if (method == 27) { d.used = 0; return kOk; }
    for (; d.next < kNodes; ++d.next) {
      const Node &n = fs.nodes[d.next];
      if (!n.used || !strcmp(n.path, d.path)) { continue; }
      parentOf(n.path, other);
      if (strcmp(other, d.path)) { continue; }
      const char *name = n.path + strlen(d.path) + (d.path[1] ? 1 : 0);
      char entry[260] = {};
      const uint32_t ino = d.next + 1;
      memcpy(entry, &ino, 4);
      copyText(entry + 4, name, 255);
      c.put(0, 0, entry, 260);
      c.put32(0, 65, 0);
      ++d.next;
      return kOk;
    }
    c.put32(0, 65, 1);
    return kOk;
  }
  case 29: {   /* mkdir(name, mode) */
    if (!c.str(1) || !normalize(c.str(1), path)) { return kNoEnt; }
    if (find(fs, path) >= 0) { return kExist; }
    return add(fs, path, true) >= 0 ? kOk : kNoSpc;
  }
  case 30: {   /* rmdir */
    if (!c.str(1) || !normalize(c.str(1), path)) { return kNoEnt; }
    const int node = find(fs, path);
    if (node < 0) { return kNoEnt; }
    if (!fs.nodes[node].dir) { return kNotDir; }
    if (hasChildren(fs, path)) { return kNotEmpty; }
    drop(fs, fs.nodes[node]);
    return kOk;
  }
  case 31: {   /* stat(name) -> 96 bytes: tsz dev ino mode nlink rdev size, then atime/mtime/ctime with nanoseconds */
    if (!c.str(1) || !normalize(c.str(1), path)) { return kNoEnt; }
    const int node = find(fs, path);
    if (node < 0) {
      ++fs.missing;
      copyText(fs.lastMissing, path, kPath);
      return kNoEnt;
    }
    const Node &n = fs.nodes[node];
    uint64_t st[12] = {};
    st[0] = 96; st[1] = 1; st[2] = uint64_t(node) + 1;
    st[3] = uint64_t(n.dir ? 040755u : 0100644u) | (uint64_t(n.dir ? 2 : 1) << 32);
    st[5] = n.size;
    c.put(0, 0, st, 96);
    return kOk;
  }
  case 32: {   /* ftrunc(FILE, offset) */
    File *f = fileOf(fs, c.word(1));
    const uint32_t to = c.word(2);
    if (!f) { return kBadF; }
    Node &n = fs.nodes[f->node];
    if (to > n.size) {
      if (!reserve(fs, n, to)) { return kNoSpc; }
      memset(n.data + n.size, 0, to - n.size);
    } else if (n.seed && !reserve(fs, n, to ? to : 1)) {
      return kNoSpc;
    }
    n.size = to;
    return kOk;
  }
  case 33: {   /* frename(old, new) */
    if (!c.str(1) || !c.str(2) || !normalize(c.str(1), path) || !normalize(c.str(2), other)) { return kNoEnt; }
    const int from = find(fs, path), to = find(fs, other);
    if (from < 0) { return kNoEnt; }
    if (fs.nodes[from].dir) { return kUnsupported; }
    if (to >= 0) {
      if (fs.nodes[to].dir) { return kIsDir; }
      for (unsigned i = 0; i < kFiles; ++i) {
        if (fs.files[i].used && fs.files[i].node == to) { fs.files[i].used = 0; }
      }
      drop(fs, fs.nodes[to]);
    }
    char parent[kPath];
    parentOf(other, parent);
    if (find(fs, parent) < 0 && add(fs, parent, true) < 0) { return kNoSpc; }
    copyText(fs.nodes[from].path, other, kPath);
    return kOk;
  }
  default:
    return kUnsupported;
  }
}
/* Name of a method for the log. */
inline const char *methodName(uint32_t method) {
  static const char *const names[37] = {"fopen", "freopen", "fflush", "fclose", "fread", "fwrite", "fgetpos", "fsetpos", "ftell", "fseek",
      "flen", "rewind", "feof", "ferror", "clearerr", "print_string", "getenv", "setenv", "unsetenv", "fopen_with_env", "fgets",
      "get_search_paths_with_env", "fileExists", "fsync", "fremove", "fdopen_decrypt", "opendir", "closedir", "readdir", "mkdir", "rmdir",
      "stat", "ftrunc", "frename", "fopen_fd", "fclose_fd", "fopen_with_env_fd"};
  return method < 37 ? names[method] : "?";
}
}  // namespace zizhan_slpi_std
#endif
