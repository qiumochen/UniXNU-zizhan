/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Host replay of drivers/PongPower/ZizhanSlpiStd.h: the registry's file traffic against a seed archive. */
#include "../../../drivers/PongPower/ZizhanSlpiStd.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace zizhan_slpi_std;
static Fs fs;
static unsigned gLive;
static void *take(uint32_t n) { ++gLive; return malloc(n); }
static void give(void *p, uint32_t) { --gLive; free(p); }
static std::string gPrinted;
static void print(const char *s) { gPrinted += s; }

struct Req {
  std::vector<std::vector<uint8_t>> in, out;
  Req &words(std::initializer_list<uint32_t> w) { std::vector<uint8_t> b(4 * w.size()); unsigned i = 0; for (uint32_t v : w) { memcpy(&b[4 * i++], &v, 4); } in.push_back(b); return *this; }
  Req &text(const char *s) { in.emplace_back(s, s + strlen(s) + 1); return *this; }
  Req &bytes(const std::vector<uint8_t> &b) { in.push_back(b); return *this; }
  Req &reply(std::initializer_list<uint32_t> sizes) { for (uint32_t n : sizes) { out.emplace_back(n, 0xee); } return *this; }
  int32_t call(uint32_t method) {
    const uint8_t *ip[8]; uint32_t il[8]; uint8_t *op[8]; uint32_t ol[8];
    for (size_t i = 0; i < in.size(); ++i) { ip[i] = in[i].empty() ? nullptr : in[i].data(); il[i] = uint32_t(in[i].size()); }
    for (size_t i = 0; i < out.size(); ++i) { op[i] = out[i].empty() ? nullptr : out[i].data(); ol[i] = uint32_t(out[i].size()); }
    const Call c{ip, il, unsigned(in.size()), op, ol, unsigned(out.size())};
    return invoke(fs, method, c);
  }
  uint32_t w(unsigned o, unsigned i) const { uint32_t v; memcpy(&v, &out[o][4 * i], 4); return v; }
};
static std::vector<uint8_t> seed(const std::vector<std::pair<std::string, std::string>> &files) {
  std::vector<uint8_t> b = {'Z', 'S', 'F', '1', 0, 0, 0, 0};
  const uint32_t count = uint32_t(files.size());
  memcpy(&b[4], &count, 4);
  for (auto &f : files) {
    const uint16_t pl = uint16_t(f.first.size()); const uint32_t dl = uint32_t(f.second.size());
    uint8_t h[8] = {}; memcpy(h, &pl, 2); memcpy(h + 4, &dl, 4);
    b.insert(b.end(), h, h + 8); b.insert(b.end(), f.first.begin(), f.first.end()); b.insert(b.end(), f.second.begin(), f.second.end());
  }
  return b;
}
static uint32_t openFile(const char *name, const char *mode, int32_t want = 0) {
  Req r; r.words({uint32_t(strlen(name) + 1), uint32_t(strlen(mode) + 1)}).text(name).text(mode).reply({4});
  const int32_t rc = r.call(0);
  if (rc != want) { printf("fopen %s %s -> %d, expected %d\n", name, mode, rc, want); abort(); }
  return rc ? 0 : r.w(0, 0);
}
static std::string readAll(uint32_t f, uint32_t chunk) {
  std::string all;
  for (;;) {
    Req r; r.words({f, chunk}).reply({8, chunk});
    assert(r.call(4) == 0);
    all.append(reinterpret_cast<const char *>(r.out[1].data()), r.w(0, 0));
    if (r.w(0, 1)) { break; }
  }
  return all;
}
int main() {
  fs.alloc = take; fs.release = give; fs.print = print; fs.ramLimit = 1u << 20;
  const std::string json(5000, 'j');
  const std::string config = "version=8\nfile=output=/mnt/vendor/persist/sensors/registry/registry\n";
  auto blob = seed({{"/vendor/etc/sensors/sns_reg_config", config},
                    {"/sys/devices/soc0/hw_platform", "MTP\n"},
                    {"/vendor/etc/sensors/config/lsm6dso_0.json", json},
                    {"/vendor/etc/sensors//config/tcs3408.json", "{}"}});
  unsigned files = 0;
  assert(mountSeed(fs, blob.data(), uint32_t(blob.size()), &files) && files == 4);
  auto bad = blob; bad[5] = 9;                       // claims more entries than the archive holds
  static Fs other; other.alloc = take; other.release = give;
  assert(!mountSeed(other, bad.data(), uint32_t(bad.size()), nullptr));

  { Req r; r.words({31, 35}).text("/vendor/etc/sensors/sns_reg_config").reply({96});      // stat, extended form resolved by the caller
    assert(r.call(31) == 0);
    uint64_t st[12]; memcpy(st, r.out[0].data(), 96);
    assert(st[0] == 96 && uint32_t(st[3]) == 0100644u && st[5] == config.size()); }
  { Req r; r.words({31, 20}).text("/vendor/etc/sensors").reply({96}); assert(r.call(31) == 0);
    uint64_t st[12]; memcpy(st, r.out[0].data(), 96); assert(uint32_t(st[3]) == 040755u); }
  { Req r; r.words({31, 6}).text("/nope").reply({96}); assert(r.call(31) == kNoEnt && fs.missing == 1 && !strcmp(fs.lastMissing, "/nope")); }

  { Req r; r.words({18, 2, 30, 2}).text("ADSP_LIBRARY_PATH").text(";").text("/sys/devices/soc0/hw_platform").text("r").reply({4});
    assert(r.call(19) == 0);
    const uint32_t f = r.w(0, 0);
    Req len; len.words({f}).reply({8}); assert(len.call(10) == 0); uint64_t n; memcpy(&n, len.out[0].data(), 8); assert(n == 4);
    assert(readAll(f, 64) == "MTP\n");
    Req eof; eof.words({f}).reply({4}); assert(eof.call(12) == 0 && eof.w(0, 0) == 1);
    Req close; close.words({f}); assert(close.call(3) == 0 && close.call(3) == kBadF); }
  { Req r; r.words({10, 1, 9, 2}).text("X").text(";").text("relative").text("r").reply({4}); assert(r.call(19) == kNoEnt); }

  // the config directory: every entry once, then the end marker
  { Req o; o.words({28}).text("/vendor/etc/sensors/config/").reply({8}); assert(o.call(26) == 0);
    uint64_t h; memcpy(&h, o.out[0].data(), 8);
    std::vector<std::string> names;
    for (;;) {
      Req r; std::vector<uint8_t> hb(8); memcpy(hb.data(), &h, 8); r.bytes(hb).reply({264});
      assert(r.call(28) == 0);
      if (r.w(0, 65)) { break; }
      names.emplace_back(reinterpret_cast<const char *>(&r.out[0][4]));
    }
    assert(names.size() == 2 && names[0] == "lsm6dso_0.json" && names[1] == "tcs3408.json");
    Req c; std::vector<uint8_t> hb(8); memcpy(hb.data(), &h, 8); c.bytes(hb); assert(c.call(27) == 0 && c.call(27) == kBadF); }
  { const uint32_t f = openFile("/vendor/etc/sensors/config/lsm6dso_0.json", "rb"); assert(readAll(f, 1024) == json);
    Req seek; seek.words({f, uint32_t(-10), 2}); assert(seek.call(9) == 0);
    Req tell; tell.words({f}).reply({4}); assert(tell.call(8) == 0 && tell.w(0, 0) == 4990);
    Req wr; wr.words({f, 1}).bytes({'x'}).reply({8}); assert(wr.call(5) == kBadF);        // opened for reading
    Req close; close.words({f}); assert(close.call(3) == 0); }

  // the registry output: directories level by level, write, read back, rename, remove
  { Req m; m.words({12, 0755}).text("/mnt/vendor"); assert(m.call(29) == 0 && m.call(29) == kExist);
    Req d; d.words({46, 0755}).text("/mnt/vendor/persist/sensors/registry/registry"); assert(d.call(29) == 0); }
  openFile("/mnt/vendor/persist/sensors/registry/registry/missing", "r", kNoEnt);
  { const uint32_t f = openFile("/mnt/vendor/persist/sensors/registry/registry/accel.tmp", "wb");
    std::vector<uint8_t> big(3000, 7);
    for (int i = 0; i < 3; ++i) { Req w; w.words({f, 3000}).bytes(big).reply({8}); assert(w.call(5) == 0 && w.w(0, 0) == 3000); }
    Req sync; sync.words({f}); assert(sync.call(23) == 0);
    Req close; close.words({f}); assert(close.call(3) == 0); }
  { Req mv; mv.words({33, 56, 52}).text("/mnt/vendor/persist/sensors/registry/registry/accel.tmp").text("/mnt/vendor/persist/sensors/registry/registry/accel");
    assert(mv.call(33) == 0);
    const uint32_t f = openFile("/mnt/vendor/persist/sensors/registry/registry/accel", "r"); assert(readAll(f, 4096).size() == 9000);
    Req close; close.words({f}); assert(close.call(3) == 0);
    Req ex; ex.words({52}).text("/mnt/vendor/persist/sensors/registry/registry/accel").reply({1}); assert(ex.call(22) == 0 && ex.out[0][0] == 1);
    Req rm; rm.words({46}).text("/mnt/vendor/persist/sensors/registry/registry"); assert(rm.call(30) == kNotEmpty);
    Req del; del.words({52}).text("/mnt/vendor/persist/sensors/registry/registry/accel"); assert(del.call(24) == 0 && del.call(24) == kNoEnt);
    assert(rm.call(30) == 0 && gLive == 0 && fs.ramBytes == 0); }

  // a seed file opened for update is copied, the archive stays untouched; append; truncate; a line reader
  { const uint32_t f = openFile("/sys/devices/soc0/hw_platform", "r+");
    Req w; w.words({f, 3}).bytes({'Q', 'R', 'D'}).reply({8}); assert(w.call(5) == 0);
    Req close; close.words({f}); assert(close.call(3) == 0);
    const uint32_t g = openFile("/sys/devices/soc0/hw_platform", "a");
    Req a; a.words({g, 2}).bytes({'!', '\n'}).reply({8}); assert(a.call(5) == 0);
    Req rw; rw.words({g}); assert(rw.call(11) == 0);
    Req line; line.words({g, 64}).reply({4, 64}); assert(line.call(20) == 0 && line.w(0, 0) == 0 && !strcmp(reinterpret_cast<char *>(line.out[1].data()), "QRD\n"));
    Req line2; line2.words({g, 64}).reply({4, 64}); assert(line2.call(20) == 0 && !strcmp(reinterpret_cast<char *>(line2.out[1].data()), "!\n"));
    Req line3; line3.words({g, 64}).reply({4, 64}); assert(line3.call(20) == 0 && line3.w(0, 0) == 1);
    Req tr; tr.words({32, g, 1}); assert(tr.call(32) == 0);
    Req len; len.words({g}).reply({8}); assert(len.call(10) == 0); uint64_t n; memcpy(&n, len.out[0].data(), 8); assert(n == 1);
    assert(std::string(blob.begin(), blob.end()).find("MTP\n") != std::string::npos); }

  // "." and ".." as the registry writes them
  { char out[kPath];
    assert(normalize("/a/b/../c/./d/", out) && !strcmp(out, "/a/c/d"));
    assert(normalize("/../..", out) && !strcmp(out, "/"));
    assert(normalize("//x///y", out) && !strcmp(out, "/x/y"));
    Req d; d.words({20, 0755}).text("/persist/reg/registry"); assert(d.call(29) == 0);
    const uint32_t f = openFile("/persist/reg/registry/../temp.json", "w");
    Req close; close.words({f}); assert(close.call(3) == 0);
    Req ex; ex.words({23}).text("/persist/reg/temp.json").reply({1}); assert(ex.call(22) == 0 && ex.out[0][0] == 1);
    Req mv; mv.words({33, 35, 30}).text("/persist/reg/registry/../temp.json").text("/persist/reg/registry/accel"); assert(mv.call(33) == 0);
    Req ex2; ex2.words({28}).text("/persist/reg/registry/accel").reply({1}); assert(ex2.call(22) == 0 && ex2.out[0][0] == 1); }

  // limits and odd input
  { fs.ramLimit = fs.ramBytes + 2048;
    const uint32_t f = openFile("/big", "w"); std::vector<uint8_t> big(4096, 1);
    Req w; w.words({f, 4096}).bytes(big).reply({8}); assert(w.call(5) == kNoSpc && w.w(0, 0) == 0); }
  { Req r; r.words({4, 2}).text("rel").text("r").reply({4}); assert(r.call(0) == kNoEnt); }
  { Req r; r.words({99, 64}).reply({8, 64}); assert(r.call(4) == kBadF); }
  { Req r; r.words({5}).text("hey\n"); assert(r.call(15) == 0 && gPrinted == "hey\n"); }
  { Req r; r.words({2, 8}).text("A").reply({4, 8}); assert(r.call(16) == kGetEnv); }
  { Req r; assert(r.call(34) == kUnsupported && r.call(1) == kUnsupported); }
  { std::string longName = "/" + std::string(200, 'a'); Req r; r.words({uint32_t(longName.size() + 1), 2}).text(longName.c_str()).text("w").reply({4}); assert(r.call(0) == kNameTooLong); }
  puts("slpi-std: PASS");
  return 0;
}
