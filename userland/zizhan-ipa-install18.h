/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Track 2 (APP-02): Pure-C Mach-O 64 / Universal binary inspector & Ad-hoc 4K code-signature
 * synthesizer for decrypted third-party IPA installation (zero OS headers; host-tested).
 *
 * Responsibilities:
 *  1. Parse thin arm64 (MH_MAGIC_64) and universal fat (FAT_CIGAM) Mach-O executables safely.
 *  2. Detect FairPlay App Store encryption (LC_ENCRYPTION_INFO_64 with cryptid != 0) and reject
 *     encrypted binaries cleanly before installation or launch.
 *  3. Parse embedded CSMAGIC_EMBEDDED_SIGNATURE (0xfade0cc0) -> CSMAGIC_CODEDIRECTORY (0xfade0c02)
 *     for both 4 KiB (shift 12) and 16 KiB (shift 14) page signatures, extracting SigningID,
 *     TeamID, embedded XML entitlements (0xfade7171), and the 20-byte SHA-256 CDHash.
 *  4. Synthesize a valid minimal Ad-hoc 4 KiB LC_CODE_SIGNATURE in-place for completely unsigned
 *     decrypted Mach-O binaries when header padding permits.
 */
#ifndef ZIZHAN_IPA_INSTALL18_H
#define ZIZHAN_IPA_INSTALL18_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ZIZHAN_IPA_OK                     = 0,
    ZIZHAN_IPA_ERR_ARGS               = -1,
    ZIZHAN_IPA_ERR_NOT_MACHO          = -2,
    ZIZHAN_IPA_ERR_NO_ARM64_SLICE     = -3,
    ZIZHAN_IPA_ERR_TRUNCATED          = -4,
    ZIZHAN_IPA_ERR_FAIRPLAY_ENCRYPTED = -5,
    ZIZHAN_IPA_ERR_NO_HEADER_SPACE    = -6,
    ZIZHAN_IPA_ERR_NO_LINKEDIT        = -7
};

typedef struct {
    uint32_t slice_offset;
    uint32_t slice_size;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint8_t  has_encryption_info;
    uint32_t cryptid;
    uint32_t cryptoff;
    uint32_t cryptsize;
    uint8_t  has_code_sig;
    uint32_t cs_offset;       /* relative to arm64 slice start */
    uint32_t cs_size;
    uint32_t cd_version;
    uint32_t cd_flags;
    uint8_t  cd_hash_type;    /* 1=SHA1, 2=SHA256 */
    uint8_t  cd_page_shift;   /* 12=4096, 14=16384 */
    uint32_t cd_page_bytes;
    uint8_t  cdhash[20];
    char     signing_id[128];
    char     team_id[32];
    uint32_t entitlements_off; /* relative to file start (0 if none) */
    uint32_t entitlements_len;
} ZizhanMachoReport;

/* --- Compact RFC 6234 SHA-256 (for real CDHash & ad-hoc page hashing) --- */
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    uint32_t buflen;
} ZizhanIpaSha256;

static inline uint32_t zizhan_ipa_rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

static inline uint32_t zizhan_ipa_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void zizhan_ipa_put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline uint32_t zizhan_ipa_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t zizhan_ipa_le64(const uint8_t *p) {
    return (uint64_t)zizhan_ipa_le32(p) | ((uint64_t)zizhan_ipa_le32(p + 4) << 32);
}

static inline void zizhan_ipa_put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void zizhan_ipa_put_le64(uint8_t *p, uint64_t v) {
    zizhan_ipa_put_le32(p, (uint32_t)v);
    zizhan_ipa_put_le32(p + 4, (uint32_t)(v >> 32));
}

static inline void zizhan_ipa_sha256_block(ZizhanIpaSha256 *ctx, const uint8_t blk[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i) w[i] = zizhan_ipa_be32(blk + i * 4u);
    for (unsigned i = 16; i < 64; ++i) {
        const uint32_t s0 = zizhan_ipa_rotr32(w[i - 15], 7) ^ zizhan_ipa_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = zizhan_ipa_rotr32(w[i - 2], 17) ^ zizhan_ipa_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (unsigned i = 0; i < 64; ++i) {
        const uint32_t S1 = zizhan_ipa_rotr32(e, 6) ^ zizhan_ipa_rotr32(e, 11) ^ zizhan_ipa_rotr32(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = h + S1 + ch + k[i] + w[i];
        const uint32_t S0 = zizhan_ipa_rotr32(a, 2) ^ zizhan_ipa_rotr32(a, 13) ^ zizhan_ipa_rotr32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static inline void zizhan_ipa_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    ZizhanIpaSha256 ctx;
    ctx.state[0] = 0x6a09e667u; ctx.state[1] = 0xbb67ae85u;
    ctx.state[2] = 0x3c6ef372u; ctx.state[3] = 0xa54ff53au;
    ctx.state[4] = 0x510e527fu; ctx.state[5] = 0x9b05688cu;
    ctx.state[6] = 0x1f83d9abu; ctx.state[7] = 0x5be0cd19u;
    ctx.bitlen = (uint64_t)len * 8ull;
    ctx.buflen = 0;
    size_t i = 0;
    while (i + 64u <= len) {
        zizhan_ipa_sha256_block(&ctx, data + i);
        i += 64u;
    }
    const size_t rem = len - i;
    if (rem) memcpy(ctx.buf, data + i, rem);
    ctx.buf[rem] = 0x80u;
    if (rem >= 56u) {
        memset(ctx.buf + rem + 1u, 0, 63u - rem);
        zizhan_ipa_sha256_block(&ctx, ctx.buf);
        memset(ctx.buf, 0, 56u);
    } else {
        memset(ctx.buf + rem + 1u, 0, 55u - rem);
    }
    zizhan_ipa_put_be32(ctx.buf + 56u, (uint32_t)(ctx.bitlen >> 32));
    zizhan_ipa_put_be32(ctx.buf + 60u, (uint32_t)ctx.bitlen);
    zizhan_ipa_sha256_block(&ctx, ctx.buf);
    for (unsigned j = 0; j < 8; ++j) zizhan_ipa_put_be32(out + j * 4u, ctx.state[j]);
}

static inline void zizhan_ipa_copy_cstr(const uint8_t *base, size_t max_len, char *dst, size_t dst_cap) {
    if (!dst || !dst_cap) return;
    size_t n = 0;
    while (n + 1u < dst_cap && n < max_len && base[n] != 0) {
        dst[n] = (char)base[n];
        ++n;
    }
    dst[n] = '\0';
}

/* Inspect a Mach-O 64 or Universal binary buffer. Returns ZIZHAN_IPA_OK (0) on decrypted valid
 * arm64 Mach-O, ZIZHAN_IPA_ERR_FAIRPLAY_ENCRYPTED (-5) when LC_ENCRYPTION_INFO_64 has cryptid != 0. */
static inline int zizhan_macho_inspect(const uint8_t *buf, size_t len, ZizhanMachoReport *out) {
    if (!buf || !out || len < 32u) return ZIZHAN_IPA_ERR_ARGS;
    memset(out, 0, sizeof(*out));

    uint32_t slice_off = 0;
    uint32_t slice_len = (len > 0xffffffffu) ? 0xffffffffu : (uint32_t)len;
    const uint32_t magic_be = zizhan_ipa_be32(buf);
    const uint32_t magic_le = zizhan_ipa_le32(buf);

    if (magic_be == 0xcafebabeu) {
        const uint32_t nfat = zizhan_ipa_be32(buf + 4u);
        if (nfat == 0u || nfat > 16u || 8u + (size_t)nfat * 20u > len) return ZIZHAN_IPA_ERR_TRUNCATED;
        int found = 0;
        for (uint32_t i = 0; i < nfat; ++i) {
            const uint8_t *fa = buf + 8u + (size_t)i * 20u;
            const uint32_t cputype = zizhan_ipa_be32(fa);
            const uint32_t foff    = zizhan_ipa_be32(fa + 8u);
            const uint32_t fsize   = zizhan_ipa_be32(fa + 12u);
            if (cputype == 0x0100000cu) { /* CPU_TYPE_ARM64 */
                if (fsize < 32u || (uint64_t)foff + (uint64_t)fsize > len) return ZIZHAN_IPA_ERR_TRUNCATED;
                slice_off = foff;
                slice_len = fsize;
                found = 1;
                break;
            }
        }
        if (!found) return ZIZHAN_IPA_ERR_NO_ARM64_SLICE;
    } else if (magic_le != 0xfeedfacfu) {
        return ZIZHAN_IPA_ERR_NOT_MACHO;
    }

    const uint8_t *mh = buf + slice_off;
    if (zizhan_ipa_le32(mh) != 0xfeedfacfu) return ZIZHAN_IPA_ERR_NOT_MACHO;
    const uint32_t cputype    = zizhan_ipa_le32(mh + 4u);
    const uint32_t cpusubtype = zizhan_ipa_le32(mh + 8u);
    const uint32_t filetype   = zizhan_ipa_le32(mh + 12u);
    const uint32_t ncmds      = zizhan_ipa_le32(mh + 16u);
    const uint32_t sizeofcmds = zizhan_ipa_le32(mh + 20u);
    if (cputype != 0x0100000cu) return ZIZHAN_IPA_ERR_NO_ARM64_SLICE;
    if (ncmds > 4096u || 32ull + (uint64_t)sizeofcmds > slice_len) return ZIZHAN_IPA_ERR_TRUNCATED;

    out->slice_offset = slice_off;
    out->slice_size   = slice_len;
    out->cputype      = cputype;
    out->cpusubtype   = cpusubtype;
    out->filetype     = filetype;

    uint32_t off = 32u;
    const uint32_t end = 32u + sizeofcmds;
    for (uint32_t i = 0; i < ncmds; ++i) {
        if (off + 8u > end) return ZIZHAN_IPA_ERR_TRUNCATED;
        const uint32_t cmd     = zizhan_ipa_le32(mh + off);
        const uint32_t cmdsize = zizhan_ipa_le32(mh + off + 4u);
        if (cmdsize < 8u || off + cmdsize > end) return ZIZHAN_IPA_ERR_TRUNCATED;

        if (cmd == 0x2cu || cmd == 0x21u) { /* LC_ENCRYPTION_INFO_64 (0x2c) or LC_ENCRYPTION_INFO (0x21) */
            if (cmdsize < 20u) return ZIZHAN_IPA_ERR_TRUNCATED;
            out->has_encryption_info = 1u;
            out->cryptoff  = zizhan_ipa_le32(mh + off + 8u);
            out->cryptsize = zizhan_ipa_le32(mh + off + 12u);
            out->cryptid   = zizhan_ipa_le32(mh + off + 16u);
        } else if (cmd == 0x1du) { /* LC_CODE_SIGNATURE */
            if (cmdsize != 16u) return ZIZHAN_IPA_ERR_TRUNCATED;
            const uint32_t dataoff  = zizhan_ipa_le32(mh + off + 8u);
            const uint32_t datasize = zizhan_ipa_le32(mh + off + 12u);
            if ((uint64_t)dataoff + (uint64_t)datasize > slice_len) return ZIZHAN_IPA_ERR_TRUNCATED;
            out->cs_offset = dataoff;
            out->cs_size   = datasize;
            if (datasize >= 12u && zizhan_ipa_be32(mh + dataoff) == 0xfade0cc0u) {
                const uint8_t *sb = mh + dataoff;
                const uint32_t sb_len = zizhan_ipa_be32(sb + 4u);
                const uint32_t sb_cnt = zizhan_ipa_be32(sb + 8u);
                if (sb_len <= datasize && sb_cnt <= 64u && 12ull + (uint64_t)sb_cnt * 8ull <= sb_len) {
                    uint32_t best_cd_off = 0, best_cd_len = 0;
                    uint8_t best_hash_type = 0;
                    for (uint32_t b = 0; b < sb_cnt; ++b) {
                        const uint32_t btype = zizhan_ipa_be32(sb + 12u + b * 8u);
                        const uint32_t boff  = zizhan_ipa_be32(sb + 16u + b * 8u);
                        if (boff + 8u > sb_len) continue;
                        const uint32_t bmagic = zizhan_ipa_be32(sb + boff);
                        const uint32_t blen   = zizhan_ipa_be32(sb + boff + 4u);
                        if (blen < 8u || (uint64_t)boff + (uint64_t)blen > sb_len) continue;
                        if (bmagic == 0xfade0c02u && blen >= 44u) { /* CSMAGIC_CODEDIRECTORY */
                            const uint8_t htype = sb[boff + 37u];
                            /* Prefer SHA-256 (hashType==2) CodeDirectory when multiple exist */
                            if (!best_cd_len || (htype == 2u && best_hash_type != 2u) || btype == 0x1000u) {
                                best_cd_off = boff;
                                best_cd_len = blen;
                                best_hash_type = htype;
                            }
                        } else if (bmagic == 0xfade7171u && blen > 8u) { /* CSMAGIC_EMBEDDED_ENTITLEMENTS */
                            out->entitlements_off = slice_off + dataoff + boff + 8u;
                            out->entitlements_len = blen - 8u;
                        }
                    }
                    if (best_cd_len >= 44u) {
                        const uint8_t *cd = sb + best_cd_off;
                        out->has_code_sig  = 1u;
                        out->cd_version    = zizhan_ipa_be32(cd + 8u);
                        out->cd_flags      = zizhan_ipa_be32(cd + 12u);
                        out->cd_hash_type  = cd[37u];
                        out->cd_page_shift = cd[39u];
                        out->cd_page_bytes = (out->cd_page_shift >= 10u && out->cd_page_shift <= 16u)
                                             ? (1u << out->cd_page_shift) : 0u;
                        const uint32_t ident_off = zizhan_ipa_be32(cd + 20u);
                        if (ident_off >= 44u && ident_off < best_cd_len) {
                            zizhan_ipa_copy_cstr(cd + ident_off, best_cd_len - ident_off,
                                                 out->signing_id, sizeof(out->signing_id));
                        }
                        if (out->cd_version >= 0x20200u && best_cd_len >= 52u) {
                            const uint32_t team_off = zizhan_ipa_be32(cd + 48u);
                            if (team_off >= 48u && team_off < best_cd_len) {
                                zizhan_ipa_copy_cstr(cd + team_off, best_cd_len - team_off,
                                                     out->team_id, sizeof(out->team_id));
                            }
                        }
                        uint8_t digest[32];
                        zizhan_ipa_sha256(cd, best_cd_len, digest);
                        memcpy(out->cdhash, digest, 20u);
                    }
                }
            }
        }
        off += cmdsize;
    }

    if (out->has_encryption_info && out->cryptid != 0u) {
        return ZIZHAN_IPA_ERR_FAIRPLAY_ENCRYPTED;
    }
    return ZIZHAN_IPA_OK;
}

/* Compute exact SuperBlob byte size needed to append an Ad-hoc 4 KiB SHA-256 signature. */
static inline size_t zizhan_macho_adhoc_blob_bound(size_t code_limit, const char *ident) {
    const size_t id_len = (ident && ident[0]) ? (strlen(ident) + 1u) : 10u;
    const size_t n_slots = (code_limit + 4095u) / 4096u;
    const size_t cd_len = 88u + ((id_len + 3u) & ~3u) + n_slots * 32u;
    return 20u + cd_len + 32u; /* SuperBlob(12) + BlobIndex(8) + CodeDirectory + alignment slack */
}

/* Sign an unsigned thin arm64 Mach-O buffer with a minimal Ad-hoc 4 KiB SHA-256 CodeDirectory.
 * `buf` must have capacity `>= cur_len + zizhan_macho_adhoc_blob_bound(cur_len, ident)`.
 * Returns 0 and writes `*new_len_out` on success. */
static inline int zizhan_macho_sign_adhoc_4k(uint8_t *buf, size_t cur_len, size_t buf_cap,
                                             const char *ident, size_t *new_len_out) {
    if (!buf || !new_len_out || cur_len < 64u) return ZIZHAN_IPA_ERR_ARGS;
    if (zizhan_ipa_le32(buf) != 0xfeedfacfu || zizhan_ipa_le32(buf + 4u) != 0x0100000cu) {
        return ZIZHAN_IPA_ERR_NOT_MACHO;
    }
    const char *sig_id = (ident && ident[0]) ? ident : "com.zizhan.adhoc";
    const uint32_t ncmds      = zizhan_ipa_le32(buf + 16u);
    const uint32_t sizeofcmds = zizhan_ipa_le32(buf + 20u);
    if (32ull + sizeofcmds > cur_len) return ZIZHAN_IPA_ERR_TRUNCATED;

    uint32_t linkedit_cmd_off = 0;
    uint32_t cs_cmd_off = 0;
    uint32_t first_sect_fileoff = (uint32_t)cur_len;
    uint64_t text_fileoff = 0, text_filesize = 0;

    uint32_t off = 32u;
    for (uint32_t i = 0; i < ncmds; ++i) {
        const uint32_t cmd     = zizhan_ipa_le32(buf + off);
        const uint32_t cmdsize = zizhan_ipa_le32(buf + off + 4u);
        if (cmdsize < 8u || off + cmdsize > 32u + sizeofcmds) return ZIZHAN_IPA_ERR_TRUNCATED;
        if (cmd == 0x19u && cmdsize >= 72u) { /* LC_SEGMENT_64 */
            if (memcmp(buf + off + 8u, "__LINKEDIT\0", 11u) == 0) {
                linkedit_cmd_off = off;
            } else if (memcmp(buf + off + 8u, "__TEXT\0", 7u) == 0) {
                text_fileoff  = zizhan_ipa_le64(buf + off + 40u);
                text_filesize = zizhan_ipa_le64(buf + off + 48u);
                const uint32_t nsects = zizhan_ipa_le32(buf + off + 64u);
                if (72ull + (uint64_t)nsects * 80ull <= cmdsize) {
                    for (uint32_t s = 0; s < nsects; ++s) {
                        const uint32_t soff = zizhan_ipa_le32(buf + off + 72u + s * 80u + 48u);
                        if (soff > 0u && soff < first_sect_fileoff) first_sect_fileoff = soff;
                    }
                }
            }
        } else if (cmd == 0x1du && cmdsize == 16u) {
            cs_cmd_off = off;
        }
        off += cmdsize;
    }
    if (!linkedit_cmd_off) return ZIZHAN_IPA_ERR_NO_LINKEDIT;

    size_t code_limit = cur_len;
    if (cs_cmd_off) {
        const uint32_t old_off = zizhan_ipa_le32(buf + cs_cmd_off + 8u);
        if (old_off >= 64u && old_off <= cur_len) code_limit = old_off;
    } else {
        if (32u + sizeofcmds + 16u > first_sect_fileoff) return ZIZHAN_IPA_ERR_NO_HEADER_SPACE;
    }
    /* Align code_limit up to 16 bytes inside __LINKEDIT */
    const size_t aligned_limit = (code_limit + 15u) & ~15u;
    const size_t id_len = strlen(sig_id) + 1u;
    const size_t id_padded = (id_len + 3u) & ~3u;
    const uint32_t n_slots = (uint32_t)((aligned_limit + 4095u) / 4096u);
    const uint32_t cd_hdr_len = 88u; /* CS_CodeDirectory v0x20400 (execSegBase/Limit/Flags included) */
    const uint32_t cd_len = cd_hdr_len + (uint32_t)id_padded + n_slots * 32u;
    const uint32_t sb_len = 20u + cd_len;
    if (aligned_limit + sb_len > buf_cap) return ZIZHAN_IPA_ERR_ARGS;

    if (aligned_limit > code_limit) {
        memset(buf + code_limit, 0, aligned_limit - code_limit);
    }

    if (!cs_cmd_off) {
        cs_cmd_off = 32u + sizeofcmds;
        zizhan_ipa_put_le32(buf + cs_cmd_off, 0x1du);
        zizhan_ipa_put_le32(buf + cs_cmd_off + 4u, 16u);
        zizhan_ipa_put_le32(buf + 16u, ncmds + 1u);
        zizhan_ipa_put_le32(buf + 20u, sizeofcmds + 16u);
    }
    zizhan_ipa_put_le32(buf + cs_cmd_off + 8u, (uint32_t)aligned_limit);
    zizhan_ipa_put_le32(buf + cs_cmd_off + 12u, sb_len);

    /* Update __LINKEDIT filesize and vmsize BEFORE hashing page 0! */
    const uint64_t le_fileoff = zizhan_ipa_le64(buf + linkedit_cmd_off + 40u);
    const uint64_t new_total  = aligned_limit + sb_len;
    if (new_total < le_fileoff) return ZIZHAN_IPA_ERR_NO_LINKEDIT;
    const uint64_t le_filesz  = new_total - le_fileoff;
    const uint64_t le_vmsz    = (le_filesz + 16383ull) & ~16383ull;
    zizhan_ipa_put_le64(buf + linkedit_cmd_off + 32u, le_vmsz);
    zizhan_ipa_put_le64(buf + linkedit_cmd_off + 48u, le_filesz);

    /* Write SuperBlob + CodeDirectory v0x20400 */
    uint8_t *sb = buf + aligned_limit;
    memset(sb, 0, sb_len);
    zizhan_ipa_put_be32(sb + 0u,  0xfade0cc0u); /* CSMAGIC_EMBEDDED_SIGNATURE */
    zizhan_ipa_put_be32(sb + 4u,  sb_len);
    zizhan_ipa_put_be32(sb + 8u,  1u);          /* count = 1 */
    zizhan_ipa_put_be32(sb + 12u, 0u);          /* CSSLOT_CODEDIRECTORY */
    zizhan_ipa_put_be32(sb + 16u, 20u);         /* offset = 20 */

    uint8_t *cd = sb + 20u;
    const uint32_t hash_off = cd_hdr_len + (uint32_t)id_padded;
    zizhan_ipa_put_be32(cd + 0u,  0xfade0c02u); /* CSMAGIC_CODEDIRECTORY */
    zizhan_ipa_put_be32(cd + 4u,  cd_len);
    zizhan_ipa_put_be32(cd + 8u,  0x00020400u); /* version 2.4 (execSeg support) */
    zizhan_ipa_put_be32(cd + 12u, 0x00000002u); /* CS_ADHOC */
    zizhan_ipa_put_be32(cd + 16u, hash_off);    /* hashOffset */
    zizhan_ipa_put_be32(cd + 20u, cd_hdr_len);  /* identOffset */
    zizhan_ipa_put_be32(cd + 24u, 0u);          /* nSpecialSlots */
    zizhan_ipa_put_be32(cd + 28u, n_slots);     /* nCodeSlots */
    zizhan_ipa_put_be32(cd + 32u, (uint32_t)aligned_limit); /* codeLimit */
    cd[36u] = 32u;                              /* hashSize = 32 (SHA-256) */
    cd[37u] = 2u;                               /* hashType = CS_HASHTYPE_SHA256 */
    cd[38u] = 0u;                               /* platform */
    cd[39u] = 12u;                              /* pageSize = 2^12 (4096) */
    zizhan_ipa_put_be32(cd + 40u, 0u);          /* spare2 */
    zizhan_ipa_put_be32(cd + 44u, 0u);          /* scatterOffset */
    zizhan_ipa_put_be32(cd + 48u, 0u);          /* teamOffset */
    zizhan_ipa_put_be32(cd + 52u, 0u);          /* spare3 */
    zizhan_ipa_put_be32(cd + 56u, 0u);          /* codeLimit64 hi */
    zizhan_ipa_put_be32(cd + 60u, 0u);          /* codeLimit64 lo */
    zizhan_ipa_put_be32(cd + 64u, (uint32_t)(text_fileoff >> 32));
    zizhan_ipa_put_be32(cd + 68u, (uint32_t)text_fileoff);
    zizhan_ipa_put_be32(cd + 72u, (uint32_t)(text_filesize >> 32));
    zizhan_ipa_put_be32(cd + 76u, (uint32_t)text_filesize);
    zizhan_ipa_put_be32(cd + 80u, 0u);          /* execSegFlags hi */
    zizhan_ipa_put_be32(cd + 84u, zizhan_ipa_le32(buf + 12u) == 2u ? 1u : 0u); /* CS_EXECSEG_MAIN_BINARY */

    memcpy(cd + cd_hdr_len, sig_id, id_len);
    for (uint32_t p = 0; p < n_slots; ++p) {
        const size_t poff = (size_t)p * 4096u;
        const size_t plen = (poff + 4096u <= aligned_limit) ? 4096u : (aligned_limit - poff);
        zizhan_ipa_sha256(buf + poff, plen, cd + hash_off + p * 32u);
    }

    *new_len_out = new_total;
    return ZIZHAN_IPA_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* ZIZHAN_IPA_INSTALL18_H */
