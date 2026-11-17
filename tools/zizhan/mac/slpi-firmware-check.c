/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host-only placement check using the kernel's actual MDT parser. No PAS,
 * memory mapping, image authentication or firmware execution is performed. */
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "../../../drivers/PongPower/QcomMdt.h"

static int file_read(void *ctx, uint64_t at, void *to, size_t bytes) {
    if (at > INT64_MAX || fseeko((FILE *)ctx, (off_t)at, SEEK_SET)) return -1;
    return fread(to, 1, bytes, (FILE *)ctx) == bytes ? 0 : -1;
}

static int fail(FILE *file, const char *stage, int code) {
    if (file) fclose(file);
    printf("{\"status\":\"FAIL\",\"stage\":\"%s\",\"code\":%d}\n", stage, code);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) return fail(NULL, "arguments", -1);
    FILE *file = fopen(argv[1], "rb");
    if (!file) return fail(NULL, "open", -1);
    if (fseeko(file, 0, SEEK_END)) return fail(file, "length", -1);
    const off_t end = ftello(file);
    if (end < 52 || end > 256 * 1024 * 1024) return fail(file, "length", -1);
    uint8_t elf[20];
    if (file_read(file, 0, elf, sizeof elf)) return fail(file, "read", -1);
    // Qualcomm DSP image, not an application-processor executable.
    if (elf[18] != 164 || elf[19] != 0) return fail(file, "elf-machine", -1);
    QcomMdtImage image;
    int result = qcom_mdt_parse(&image, (uint64_t)end, file_read, file);
    if (result) return fail(file, "mdt-parse", result);
    if (image.max_address > UINT64_MAX - 4095 ||
        image.min_address > image.max_address)
        return fail(file, "span-overflow", -1);
    const uint64_t span = qcom_mdt_span(&image);
    // This CLI checks only the reviewed zizhan SLPI carveout.
    const uint64_t base = 0x88000000ULL, bytes = 0x1900000ULL;
    if (span > bytes) return fail(file, "carveout-span", QcomMdtOutside);
    const uint64_t metadata_limit = 16 * 1024 * 1024;
    const uint64_t hash_bytes = image.segment[image.hash].file_size;
    if (!hash_bytes) return fail(file, "empty-hash", QcomMdtNoHash);
    if (image.header_size > metadata_limit || hash_bytes > metadata_limit - image.header_size)
        return fail(file, "metadata-limit", QcomMdtOutside);
    const uint64_t metadata_bytes = qcom_mdt_metadata_size(&image);
    uint8_t *metadata = malloc((size_t)metadata_bytes);
    if (!metadata) return fail(file, "metadata-allocation", -1);
    result = qcom_mdt_metadata(&image, file_read, file, metadata, metadata_bytes);
    free(metadata);
    if (result) return fail(file, "metadata-read", result);
    unsigned loads = 0;
    for (unsigned i = 0; i < image.count; ++i) {
        if (!image.segment[i].load) continue;
        uint64_t offset = 0;
        result = qcom_mdt_place(&image, i, base, bytes, &offset);
        if (result) return fail(file, "segment-placement", result);
        ++loads;
    }
    fclose(file);
    printf("{\"status\":\"PASS\",\"scope\":\"host-placement-only\","
           "\"elf64\":%u,\"segments\":%u,\"load_segments\":%u,"
           "\"relocatable\":%u,\"span\":%llu,\"metadata_bytes\":%llu,"
           "\"carveout_base\":%llu,\"carveout_bytes\":%llu,"
           "\"authenticated\":false,\"started\":false}\n",
           image.elf64, image.count, loads, image.relocatable,
           (unsigned long long)span, (unsigned long long)metadata_bytes,
           (unsigned long long)base, (unsigned long long)bytes);
    return 0;
}
