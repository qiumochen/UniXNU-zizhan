/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_SCENE_ORIENT18_H
#define ZIZHAN_SCENE_ORIENT18_H

/* Pure-C cold-launch initial orientation policy for SBDeviceApplicationSceneHandle.
 *
 * In 22H374 SpringBoard, -[SBDeviceApplicationSceneHandle _modifyApplicationSceneSettings:fromRequestContext:entity:]
 * looks up its TRAParticipant via SBTraitsSceneParticipantManager; on initial cold scene creation ([self sceneIfExists] == nil),
 * the switcher has not registered the participant yet, so the fallback path leaves settings.interfaceOrientation at 1
 * (UIInterfaceOrientationPortrait, 1080x958 sideways on Mix Fold 2's scanout) for Frame 0 before diffing 1 -> 4.
 *
 * Seed initial settings ONLY when scene_exists == 0, honoring SBOrientationLockManager first (when user_locked),
 * then SpringBoard's activeInterfaceOrientation, and strictly respecting _mainSceneSupportsInterfaceOrientation:. */
#include <string.h>

/* Map an Info.plist UISupportedInterfaceOrientations string to its UIInterfaceOrientationMask bit. */
static inline unsigned long zizhan_orient_mask_from_name(const char *name)
{
    if (!name) {
        return 0;
    }
    if (!strcmp(name, "UIInterfaceOrientationPortrait")) {
        return 1UL << 1;
    }
    if (!strcmp(name, "UIInterfaceOrientationPortraitUpsideDown")) {
        return 1UL << 2;
    }
    if (!strcmp(name, "UIInterfaceOrientationLandscapeRight")) {
        return 1UL << 3;
    }
    if (!strcmp(name, "UIInterfaceOrientationLandscapeLeft")) {
        return 1UL << 4;
    }
    return 0;
}

/* Compute effective cold-launch supported orientation mask on iPad identity before the app's
 * first uiClientSettings arrives:
 * - Classic apps or non-Pad idioms keep orig_mask unchanged.
 * - If Info.plist has UISupportedInterfaceOrientations~ipad (ipad_plist_mask != 0), union it in
 *   while client_mask == 0 (fixes FBSApplicationInfo returning 0x2 from ~iphone before launch).
 * - If a modern scene-manifest iPad app omits both ~ipad and base keys (orig_mask == 0 && !has_any_plist_key),
 *   default to 0x1e (all 4 orientations) until uiClientSettings arrives. */
static inline unsigned long zizhan_effective_pad_orient_mask(int is_pad_idiom, int is_classic,
                                                             int has_client_settings,
                                                             unsigned long orig_mask,
                                                             unsigned long ipad_plist_mask,
                                                             int has_any_plist_key)
{
    if (!is_pad_idiom || is_classic || has_client_settings) {
        return orig_mask;
    }
    if (ipad_plist_mask != 0) {
        return orig_mask | ipad_plist_mask;
    }
    if (orig_mask == 0 && !has_any_plist_key) {
        return 0x1eUL;
    }
    return orig_mask;
}

static inline long zizhan_scene_orient_candidate(int enabled, int scene_exists,
                                                 int user_locked, long lock_orient,
                                                 long active_orient)
{
    if (!enabled || scene_exists) {
        return 0;
    }
    if (user_locked && lock_orient >= 1 && lock_orient <= 4) {
        return lock_orient;
    }
    if (active_orient >= 1 && active_orient <= 4) {
        return active_orient;
    }
    return 0;
}

static inline long zizhan_scene_orient_apply(long candidate, long current_orient,
                                             int scene_supports_candidate)
{
    if (candidate < 1 || candidate > 4 || !scene_supports_candidate || candidate == current_orient) {
        return 0;
    }
    return candidate;
}

#endif
