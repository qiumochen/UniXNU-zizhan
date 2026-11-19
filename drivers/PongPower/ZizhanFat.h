/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* MODEM-01 stage 1: read-only GPT + FAT12/16/32 reader for the Android firmware partitions (modem_a/b = NON-HLOS, vfat),
 * so the modem image can be loaded straight from UFS LUN 4 without copying anything to the phone. Names match either the
 * 8.3 short name or the VFAT long name (checksummed; the real partition stores "image" only as a long name, short name
 * "PCAFUI~2"), case-insensitively, ASCII only. Pure C++, no kernel headers:
 * all storage goes through a caller-provided read callback (byte offset relative to the LUN), so the same code runs in host
 * tests against a real partition image. Every loop is bounded; a malformed volume makes the call fail, never spin. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace zizhan_fat {

typedef int (*ReadFn)(void *ctx, uint64_t byte_offset, void *dst, uint32_t length);   // 0 = ok

static inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
static inline uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
static inline uint64_t le64(const uint8_t *p) { return uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32); }

/* GPT on a LUN with `block` byte logical blocks. Finds the entry whose UTF-16LE name equals `name` (ASCII). */
struct GptPart { uint64_t first_lba, last_lba; };
static inline int gpt_find(ReadFn read, void *ctx, uint32_t block, const char *name, GptPart *out) {
  uint8_t hdr[92];
  if (block < 512 || block > 4096 || read(ctx, uint64_t(block), hdr, sizeof hdr)) return -1;
  if (memcmp(hdr, "EFI PART", 8)) return -2;
  const uint64_t entries_lba = le64(hdr + 72);
  const uint32_t count = le32(hdr + 80), size = le32(hdr + 84);
  if (size < 128 || size > 512 || count == 0 || count > 1024) return -3;
  const size_t want = strlen(name);
  if (want == 0 || want > 36) return -4;
  uint8_t e[512];
  for (uint32_t i = 0; i < count; ++i) {
    if (read(ctx, entries_lba * block + uint64_t(i) * size, e, size)) return -5;
    bool zero = true;
    for (unsigned k = 0; k < 16; ++k) { if (e[k]) { zero = false; break; } }
    if (zero) continue;
    bool match = true;
    for (size_t k = 0; k < 36 && match; ++k) {
      const uint16_t c = le16(e + 56 + 2 * k);
      const uint16_t w = k < want ? uint16_t(uint8_t(name[k])) : 0;
      if (c != w) { match = false; }
      if (!w) { break; }
    }
    if (match) {
      out->first_lba = le64(e + 32);
      out->last_lba = le64(e + 40);
      return out->last_lba >= out->first_lba ? 0 : -6;
    }
  }
  return -7;
}

struct Volume {
  ReadFn read; void *ctx; uint64_t base;          // partition start, bytes from the LUN start
  uint32_t bytes_per_sector, sectors_per_cluster, cluster_bytes;
  uint64_t fat_offset, root_offset, data_offset;  // bytes from the partition start
  uint32_t root_entries, root_cluster, cluster_count, type;   // type 12 / 16 / 32
  uint32_t fat_cache_sector; uint8_t fat_cache[4096]; bool fat_cache_valid;
};

static inline int mount(Volume *v, ReadFn read, void *ctx, uint64_t base) {
  memset(v, 0, sizeof *v);
  v->read = read; v->ctx = ctx; v->base = base;
  uint8_t bs[512];
  if (read(ctx, base, bs, sizeof bs)) return -1;
  if (bs[510] != 0x55 || bs[511] != 0xaa) return -2;
  const uint32_t bps = le16(bs + 11), spc = bs[13], reserved = le16(bs + 14), fats = bs[16], root = le16(bs + 17);
  const uint32_t total = le16(bs + 19) ? le16(bs + 19) : le32(bs + 32);
  const uint32_t fat_sectors = le16(bs + 22) ? le16(bs + 22) : le32(bs + 36);
  if ((bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) || !spc || (spc & (spc - 1)) || spc > 128 ||
      !reserved || !fats || fats > 4 || !fat_sectors || !total) return -3;
  const uint32_t root_sectors = (root * 32 + bps - 1) / bps;
  const uint64_t meta = uint64_t(reserved) + uint64_t(fats) * fat_sectors + root_sectors;
  if (meta >= total) return -4;
  v->bytes_per_sector = bps; v->sectors_per_cluster = spc; v->cluster_bytes = bps * spc;
  if (v->cluster_bytes > 256 * 1024) return -5;
  v->cluster_count = uint32_t((total - meta) / spc);
  v->type = v->cluster_count < 4085 ? 12 : v->cluster_count < 65525 ? 16 : 32;
  v->fat_offset = uint64_t(reserved) * bps;
  v->root_offset = v->fat_offset + uint64_t(fats) * fat_sectors * bps;
  v->data_offset = v->root_offset + uint64_t(root_sectors) * bps;
  v->root_entries = root;
  v->root_cluster = v->type == 32 ? le32(bs + 44) : 0;
  if (v->type == 32 && (root || v->root_cluster < 2)) return -6;
  if (v->type != 32 && !root) return -7;
  return 0;
}

static inline uint64_t cluster_offset(const Volume *v, uint32_t cluster) {
  return v->data_offset + uint64_t(cluster - 2) * v->cluster_bytes;
}

/* Next cluster in the chain; 0 at the end of the chain or on any invalid entry. */
static inline uint32_t next_cluster(Volume *v, uint32_t cluster) {
  if (cluster < 2 || cluster >= v->cluster_count + 2) return 0;
  uint64_t at;
  switch (v->type) {
    case 12: at = cluster + cluster / 2; break;
    case 16: at = uint64_t(cluster) * 2; break;
    default: at = uint64_t(cluster) * 4; break;
  }
  uint8_t raw[4];
  for (unsigned i = 0; i < (v->type == 32 ? 4u : 2u); ++i) {
    const uint64_t byte = at + i;
    const uint32_t sector = uint32_t(byte / v->bytes_per_sector);
    if (!v->fat_cache_valid || v->fat_cache_sector != sector) {
      if (v->read(v->ctx, v->base + v->fat_offset + uint64_t(sector) * v->bytes_per_sector, v->fat_cache, v->bytes_per_sector)) return 0;
      v->fat_cache_sector = sector; v->fat_cache_valid = true;
    }
    raw[i] = v->fat_cache[byte % v->bytes_per_sector];
  }
  uint32_t n;
  if (v->type == 12) { const uint16_t w = uint16_t(raw[0] | (raw[1] << 8)); n = (cluster & 1) ? (w >> 4) : (w & 0xfff); if (n >= 0xff7) return 0; }
  else if (v->type == 16) { n = uint32_t(raw[0] | (raw[1] << 8)); if (n >= 0xfff7) return 0; }
  else { n = le32(raw) & 0x0fffffff; if (n >= 0x0ffffff7) return 0; }
  return (n >= 2 && n < v->cluster_count + 2) ? n : 0;
}

/* "modem.b23" -> "MODEM   B23"; false if the name cannot be an 8.3 short name. */
static inline bool short_name(const char *name, size_t length, char out[11]) {
  memset(out, ' ', 11);
  size_t i = 0, k = 0;
  for (; i < length && name[i] != '.'; ++i) { if (k >= 8) return false; char c = name[i]; if (c >= 'a' && c <= 'z') c = char(c - 32); out[k++] = c; }
  if (!k) return false;
  if (i < length) {
    ++i; k = 8;
    for (; i < length; ++i) { if (k >= 11 || name[i] == '.') return false; char c = name[i]; if (c >= 'a' && c <= 'z') c = char(c - 32); out[k++] = c; }
  }
  return true;
}

struct File { uint32_t first_cluster, size; bool directory; uint32_t cur_index, cur_cluster; };

static inline uint8_t lfn_checksum(const uint8_t *n83) {
  uint8_t sum = 0;
  for (unsigned i = 0; i < 11; ++i) { sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + n83[i]); }
  return sum;
}
static inline char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

/* Look a name up in a directory (cluster 0 = the FAT12/16 fixed root). */
static inline int dir_find(Volume *v, uint32_t dir_cluster, const char *name, size_t length, File *out) {
  char n83[11];
  const bool has_short = short_name(name, length, n83);
  if (length == 0 || length > 255) return -10;
  uint8_t ent[32];
  char lfn[260];
  unsigned lfn_parts = 0, lfn_seen = 0;
  uint8_t lfn_sum = 0;
  bool lfn_ok = false;
  static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
  const bool fixed_root = v->type != 32 && dir_cluster == 0;
  uint32_t cluster = fixed_root ? 0 : (dir_cluster ? dir_cluster : v->root_cluster);
  for (unsigned hops = 0; hops < 65536; ++hops) {
    const uint32_t per = fixed_root ? v->root_entries : v->cluster_bytes / 32;
    const uint64_t at = fixed_root ? v->root_offset : cluster_offset(v, cluster);
    for (uint32_t i = 0; i < per; ++i) {
      if (v->read(v->ctx, v->base + at + uint64_t(i) * 32, ent, sizeof ent)) return -1;
      if (ent[0] == 0x00) return -2;                   // end of directory
      if (ent[0] == 0xe5) { lfn_ok = false; continue; }  // deleted
      if (ent[11] == 0x0f) {                           // long-name fragment, last one first
        const unsigned ord = ent[0] & 0x3f;
        if (ent[0] & 0x40) { lfn_parts = ord; lfn_seen = 0; lfn_sum = ent[13]; lfn_ok = ord >= 1 && ord <= 20; memset(lfn, 0, sizeof lfn); }
        if (!lfn_ok || ord != lfn_parts - lfn_seen || ent[13] != lfn_sum) { lfn_ok = false; continue; }
        for (unsigned k = 0; k < 13; ++k) {
          const uint16_t c = le16(ent + pos[k]);
          const unsigned at_char = (ord - 1) * 13 + k;
          if (c == 0 || c == 0xffff || at_char >= 255) break;
          lfn[at_char] = c < 0x80 ? char(c) : '?';
        }
        ++lfn_seen;
        continue;
      }
      if (ent[11] & 0x08) { lfn_ok = false; continue; }   // volume label
      bool match = has_short && !memcmp(ent, n83, 11);
      if (!match && lfn_ok && lfn_seen == lfn_parts && lfn_checksum(ent) == lfn_sum && strlen(lfn) == length) {
        match = true;
        for (size_t k = 0; k < length && match; ++k) { match = ascii_lower(lfn[k]) == ascii_lower(name[k]); }
      }
      lfn_ok = false;
      if (!match) continue;
      out->first_cluster = (uint32_t(le16(ent + 20)) << 16) | le16(ent + 26);
      if (v->type != 32) out->first_cluster &= 0xffff;
      out->size = le32(ent + 28);
      out->directory = (ent[11] & 0x10) != 0;
      out->cur_index = 0; out->cur_cluster = out->first_cluster;
      return 0;
    }
    if (fixed_root) return -2;
    cluster = next_cluster(v, cluster);
    if (!cluster) return -2;
  }
  return -3;
}

/* "image/modem.mdt" relative to the root. */
static inline int open_path(Volume *v, const char *path, File *out) {
  uint32_t dir = 0;
  const char *p = path;
  for (unsigned depth = 0; depth < 8; ++depth) {
    const char *slash = strchr(p, '/');
    const size_t len = slash ? size_t(slash - p) : strlen(p);
    File f;
    const int rc = dir_find(v, dir, p, len, &f);
    if (rc) return rc;
    if (!slash) { if (f.directory) return -11; *out = f; return 0; }
    if (!f.directory) return -12;
    dir = f.first_cluster;
    if (!dir) return -13;
    p = slash + 1;
  }
  return -14;
}

/* Read [offset, offset+length) of a file. Sequential calls are O(1) per cluster thanks to the cursor in File. */
static inline int file_read(Volume *v, File *f, uint64_t offset, void *dst, uint32_t length) {
  if (offset > f->size || length > f->size - offset) return -1;
  uint8_t *out = static_cast<uint8_t *>(dst);
  while (length) {
    const uint32_t index = uint32_t(offset / v->cluster_bytes), within = uint32_t(offset % v->cluster_bytes);
    if (index < f->cur_index) { f->cur_index = 0; f->cur_cluster = f->first_cluster; }
    while (f->cur_index < index) {
      f->cur_cluster = next_cluster(v, f->cur_cluster);
      if (!f->cur_cluster) return -2;
      ++f->cur_index;
    }
    if (f->cur_cluster < 2) return -3;
    const uint32_t chunk = length < v->cluster_bytes - within ? length : v->cluster_bytes - within;
    if (v->read(v->ctx, v->base + cluster_offset(v, f->cur_cluster) + within, out, chunk)) return -4;
    out += chunk; offset += chunk; length -= chunk;
  }
  return 0;
}

}  // namespace zizhan_fat
