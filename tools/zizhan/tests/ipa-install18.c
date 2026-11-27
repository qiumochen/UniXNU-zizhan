/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../../../userland/zizhan-ipa-install18.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void build_synthetic_macho64(uint8_t *buf, size_t total_len, int with_encrypt, uint32_t cryptid) {
    memset(buf, 0, total_len);
    /* mach_header_64 */
    zizhan_ipa_put_le32(buf + 0,  0xfeedfacfu);
    zizhan_ipa_put_le32(buf + 4,  0x0100000cu); /* CPU_TYPE_ARM64 */
    zizhan_ipa_put_le32(buf + 8,  0u);
    zizhan_ipa_put_le32(buf + 12, 2u);          /* MH_EXECUTE */

    uint32_t off = 32u;
    uint32_t ncmds = 0;

    /* LC_SEGMENT_64 __TEXT (72 + 1 section * 80 = 152 bytes) */
    zizhan_ipa_put_le32(buf + off + 0, 0x19u);
    zizhan_ipa_put_le32(buf + off + 4, 152u);
    memcpy(buf + off + 8, "__TEXT", 6);
    zizhan_ipa_put_le64(buf + off + 24, 0x100000000ull);
    zizhan_ipa_put_le64(buf + off + 32, 0x4000ull);
    zizhan_ipa_put_le64(buf + off + 40, 0ull);
    zizhan_ipa_put_le64(buf + off + 48, 0x4000ull);
    zizhan_ipa_put_le32(buf + off + 56, 5u);
    zizhan_ipa_put_le32(buf + off + 60, 5u);
    zizhan_ipa_put_le32(buf + off + 64, 1u); /* 1 section */
    /* section_64 __text at fileoff 0x800 */
    memcpy(buf + off + 72, "__text", 6);
    memcpy(buf + off + 88, "__TEXT", 6);
    zizhan_ipa_put_le32(buf + off + 72 + 48, 0x800u);
    off += 152u;
    ++ncmds;

    /* LC_SEGMENT_64 __LINKEDIT at fileoff 0x4000 */
    zizhan_ipa_put_le32(buf + off + 0, 0x19u);
    zizhan_ipa_put_le32(buf + off + 4, 72u);
    memcpy(buf + off + 8, "__LINKEDIT", 10);
    zizhan_ipa_put_le64(buf + off + 24, 0x100004000ull);
    zizhan_ipa_put_le64(buf + off + 32, 0x4000ull);
    zizhan_ipa_put_le64(buf + off + 40, 0x4000ull);
    zizhan_ipa_put_le64(buf + off + 48, total_len - 0x4000ull);
    zizhan_ipa_put_le32(buf + off + 56, 1u);
    zizhan_ipa_put_le32(buf + off + 60, 1u);
    off += 72u;
    ++ncmds;

    if (with_encrypt) {
        zizhan_ipa_put_le32(buf + off + 0, 0x2cu); /* LC_ENCRYPTION_INFO_64 */
        zizhan_ipa_put_le32(buf + off + 4, 24u);
        zizhan_ipa_put_le32(buf + off + 8, 0x800u);
        zizhan_ipa_put_le32(buf + off + 12, 0x1000u);
        zizhan_ipa_put_le32(buf + off + 16, cryptid);
        off += 24u;
        ++ncmds;
    }

    zizhan_ipa_put_le32(buf + 16, ncmds);
    zizhan_ipa_put_le32(buf + 20, off - 32u);
    /* Put deterministic arm64 ret instruction in __text */
    zizhan_ipa_put_le32(buf + 0x800u, 0xd65f03c0u);
}

static void test_sha256_vector(void) {
    /* NIST SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
    uint8_t dig[32];
    zizhan_ipa_sha256((const uint8_t *)"abc", 3, dig);
    static const uint8_t kExp[8] = {0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau};
    assert(memcmp(dig, kExp, 8) == 0);
}

static void test_fairplay_rejection_and_decrypted_pass(void) {
    uint8_t bin[0x4200];
    ZizhanMachoReport rep;

    /* FairPlay encrypted (cryptid = 1) must be rejected cleanly */
    build_synthetic_macho64(bin, sizeof(bin), 1, 1u);
    assert(zizhan_macho_inspect(bin, sizeof(bin), &rep) == ZIZHAN_IPA_ERR_FAIRPLAY_ENCRYPTED);
    assert(rep.has_encryption_info == 1u && rep.cryptid == 1u);

    /* Decrypted (cryptid = 0) passes inspection */
    build_synthetic_macho64(bin, sizeof(bin), 1, 0u);
    assert(zizhan_macho_inspect(bin, sizeof(bin), &rep) == ZIZHAN_IPA_OK);
    assert(rep.has_encryption_info == 1u && rep.cryptid == 0u);
    assert(rep.has_code_sig == 0u);
}

static void test_adhoc_4k_sign_and_re_inspect(void) {
    const size_t base_len = 0x4100u;
    const size_t cap = base_len + zizhan_macho_adhoc_blob_bound(base_len, "com.zizhan.demoapp");
    uint8_t *bin = (uint8_t *)calloc(1, cap);
    assert(bin != NULL);
    build_synthetic_macho64(bin, base_len, 1, 0u);

    size_t signed_len = 0;
    assert(zizhan_macho_sign_adhoc_4k(bin, base_len, cap, "com.zizhan.demoapp", &signed_len) == ZIZHAN_IPA_OK);
    assert(signed_len > base_len && signed_len <= cap);

    ZizhanMachoReport rep;
    assert(zizhan_macho_inspect(bin, signed_len, &rep) == ZIZHAN_IPA_OK);
    assert(rep.has_code_sig == 1u);
    assert(rep.cd_page_shift == 12u && rep.cd_page_bytes == 4096u);
    assert(rep.cd_hash_type == 2u);
    assert(strcmp(rep.signing_id, "com.zizhan.demoapp") == 0);
    assert(rep.entitlements_len > 64u && rep.entitlements_len < 640u);
    {
        char xml_copy[640];
        memcpy(xml_copy, bin + rep.entitlements_off, rep.entitlements_len);
        xml_copy[rep.entitlements_len] = '\0';
        assert(strstr(xml_copy, "keychain-access-groups") != NULL);
        assert(strstr(xml_copy, "group.com.zizhan.shared") != NULL);
    }
    int nonzero_cdhash = 0;
    for (unsigned i = 0; i < 20; ++i) nonzero_cdhash |= rep.cdhash[i];
    assert(nonzero_cdhash != 0);

    /* Also wrap inside a Universal FAT header (x86_64 dummy slice + arm64 signed slice) */
    const size_t fat_off = 0x1000u;
    const size_t fat_len = fat_off + signed_len;
    uint8_t *fat = (uint8_t *)calloc(1, fat_len);
    assert(fat != NULL);
    zizhan_ipa_put_be32(fat + 0, 0xcafebabeu);
    zizhan_ipa_put_be32(fat + 4, 2u);
    /* arch 0: x86_64 */
    zizhan_ipa_put_be32(fat + 8,  0x01000007u);
    zizhan_ipa_put_be32(fat + 16, 0x200u);
    zizhan_ipa_put_be32(fat + 20, 64u);
    /* arch 1: arm64 */
    zizhan_ipa_put_be32(fat + 28, 0x0100000cu);
    zizhan_ipa_put_be32(fat + 36, (uint32_t)fat_off);
    zizhan_ipa_put_be32(fat + 40, (uint32_t)signed_len);
    memcpy(fat + fat_off, bin, signed_len);

    ZizhanMachoReport frep;
    assert(zizhan_macho_inspect(fat, fat_len, &frep) == ZIZHAN_IPA_OK);
    assert(frep.slice_offset == fat_off);
    assert(frep.has_code_sig == 1u);
    assert(memcmp(frep.cdhash, rep.cdhash, 20) == 0);

    free(fat);
    free(bin);
}

static void test_16k_pagesize_and_entitlements_inspect(void) {
    uint8_t bin[0x4400];
    build_synthetic_macho64(bin, 0x4100u, 0, 0u);
    /* Manually attach a 16K-page (shift=14) CodeDirectory + XML Entitlements blob (0xfade7171) */
    const uint32_t ncmds = zizhan_ipa_le32(bin + 16u);
    const uint32_t sizeofcmds = zizhan_ipa_le32(bin + 20u);
    const uint32_t cs_off = 0x4100u;
    const char *xml = "<?xml version=\"1.0\"?><plist><dict><key>get-task-allow</key><true/></dict></plist>";
    const uint32_t xml_len = (uint32_t)strlen(xml);
    const uint32_t ent_blob_len = 8u + xml_len;
    const uint32_t cd_len = 64u;
    const uint32_t sb_len = 28u + cd_len + ent_blob_len;

    zizhan_ipa_put_le32(bin + 32u + sizeofcmds + 0, 0x1du);
    zizhan_ipa_put_le32(bin + 32u + sizeofcmds + 4, 16u);
    zizhan_ipa_put_le32(bin + 32u + sizeofcmds + 8, cs_off);
    zizhan_ipa_put_le32(bin + 32u + sizeofcmds + 12, sb_len);
    zizhan_ipa_put_le32(bin + 16u, ncmds + 1u);
    zizhan_ipa_put_le32(bin + 20u, sizeofcmds + 16u);

    uint8_t *sb = bin + cs_off;
    zizhan_ipa_put_be32(sb + 0, 0xfade0cc0u);
    zizhan_ipa_put_be32(sb + 4, sb_len);
    zizhan_ipa_put_be32(sb + 8, 2u);
    zizhan_ipa_put_be32(sb + 12, 0u);
    zizhan_ipa_put_be32(sb + 16, 28u);
    zizhan_ipa_put_be32(sb + 20, 5u);
    zizhan_ipa_put_be32(sb + 24, 28u + cd_len);

    uint8_t *cd = sb + 28u;
    zizhan_ipa_put_be32(cd + 0, 0xfade0c02u);
    zizhan_ipa_put_be32(cd + 4, cd_len);
    zizhan_ipa_put_be32(cd + 8, 0x20400u);
    zizhan_ipa_put_be32(cd + 20, 48u); /* ident at +48 */
    cd[36] = 32u;
    cd[37] = 2u;
    cd[39] = 14u; /* 16384-byte page signature! */
    memcpy(cd + 48u, "com.16k.app", 12u);

    uint8_t *eb = sb + 28u + cd_len;
    zizhan_ipa_put_be32(eb + 0, 0xfade7171u);
    zizhan_ipa_put_be32(eb + 4, ent_blob_len);
    memcpy(eb + 8u, xml, xml_len);

    ZizhanMachoReport rep;
    assert(zizhan_macho_inspect(bin, cs_off + sb_len, &rep) == ZIZHAN_IPA_OK);
    assert(rep.has_code_sig == 1u);
    assert(rep.cd_page_shift == 14u && rep.cd_page_bytes == 16384u);
    assert(strcmp(rep.signing_id, "com.16k.app") == 0);
    assert(rep.entitlements_len == xml_len);
    assert(memcmp(bin + rep.entitlements_off, xml, xml_len) == 0);
}

int main(void) {
    test_sha256_vector();
    test_fairplay_rejection_and_decrypted_pass();
    test_adhoc_4k_sign_and_re_inspect();
    test_16k_pagesize_and_entitlements_inspect();
    puts("PASS ipa-install18");
    return 0;
}
