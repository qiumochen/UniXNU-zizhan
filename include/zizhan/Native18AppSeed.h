/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_NATIVE18_APP_SEED_H
#define ZIZHAN_NATIVE18_APP_SEED_H

/* The native removable-app manifest is separate from the ordinary var seed.
 * Only its exact numbered manifest may copy the matching relative app path
 * into staged_system_apps. Native SystemAppMigrator still installs the apps.
 * Callers also enforce data_path(), owned manifests and O_EXCL/O_NOFOLLOW.
 */
static int zizhan_native18_app_seed(const char *manifest, const char *source,
                                  const char *destination)
{
    static const char manifest_prefix[] = "/pong/etc/native-apps-";
    static const char source_prefix[] = "/pong/native-staged-apps/";
    static const char destination_prefix[] = "/private/var/staged_system_apps/";
    const unsigned prefix = sizeof(manifest_prefix) - 1;
    if (strncmp(manifest, manifest_prefix, prefix) || strlen(manifest) != prefix + 8)
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (manifest[prefix + i] < '0' || manifest[prefix + i] > '9')
            return 0;
    if (strcmp(manifest + prefix + 3, ".skel") ||
        strncmp(source, source_prefix, sizeof(source_prefix) - 1) ||
        strncmp(destination, destination_prefix, sizeof(destination_prefix) - 1))
        return 0;
    const char *relative = source + sizeof(source_prefix) - 1;
    if (!*relative || strcmp(relative, destination + sizeof(destination_prefix) - 1))
        return 0;
    return 1;
}

#endif
