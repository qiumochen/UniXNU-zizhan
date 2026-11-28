/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include "zizhan-scene-orient18.h"

int main(void)
{
    /* 1. Rotation-locked to upright (wori=4): cold launch seeds 4 when initial settings had 1. */
    long cand = zizhan_scene_orient_candidate(1, 0, 1, 4, 1);
    assert(cand == 4);
    assert(zizhan_scene_orient_apply(cand, 1, 1) == 4);

    /* 2. Already existing live scene (scene_exists == 1): never override live scene transitions. */
    assert(zizhan_scene_orient_candidate(1, 1, 1, 4, 4) == 0);

    /* 3. Disabled via zizhan_scene_orient=0: leaves native behavior untouched. */
    assert(zizhan_scene_orient_candidate(0, 0, 1, 4, 4) == 0);

    /* 4. Unlocked, SpringBoard activeInterfaceOrientation is 4 (upright) or 3 (upside-down): seeds active. */
    assert(zizhan_scene_orient_candidate(1, 0, 0, 0, 4) == 4);
    assert(zizhan_scene_orient_apply(4, 1, 1) == 4);
    assert(zizhan_scene_orient_candidate(1, 0, 0, 0, 3) == 3);
    assert(zizhan_scene_orient_apply(3, 1, 1) == 3);

    /* 5. App does not support candidate orientation (e.g. portrait-only app): respected, returns 0. */
    assert(zizhan_scene_orient_apply(4, 1, 0) == 0);

    /* 6. Initial settings already match candidate: returns 0 (no redundant mutation). */
    assert(zizhan_scene_orient_apply(4, 4, 1) == 0);

    /* 7. Out-of-range lock falls back to valid active orientation; out-of-range active returns 0. */
    assert(zizhan_scene_orient_candidate(1, 0, 1, 0, 2) == 2);
    assert(zizhan_scene_orient_candidate(1, 0, 0, 0, 0) == 0);
    assert(zizhan_scene_orient_candidate(1, 0, 1, 9, 5) == 0);

    /* 8. UISupportedInterfaceOrientations~ipad mask resolution before uiClientSettings arrives. */
    unsigned long pad4 = zizhan_orient_mask_from_name("UIInterfaceOrientationPortrait") |
                         zizhan_orient_mask_from_name("UIInterfaceOrientationPortraitUpsideDown") |
                         zizhan_orient_mask_from_name("UIInterfaceOrientationLandscapeLeft") |
                         zizhan_orient_mask_from_name("UIInterfaceOrientationLandscapeRight");
    assert(pad4 == 0x1eUL);
    assert(zizhan_orient_mask_from_name("Unknown") == 0);
    /* Preferences before Frame 0: orig_mask=0x2 (~iphone), ipad_plist_mask=0x1e -> 0x1e. */
    assert(zizhan_effective_pad_orient_mask(1, 0, 0, 0x2UL, pad4, 1) == 0x1eUL);
    /* Portrait-only iPad app: ipad_plist_mask=0x2 -> stays 0x2 (never forced landscape). */
    assert(zizhan_effective_pad_orient_mask(1, 0, 0, 0x2UL, 0x2UL, 1) == 0x2UL);
    /* Classic iPhone app on iPad: keeps orig_mask=0x2 even if ipad_plist_mask were set. */
    assert(zizhan_effective_pad_orient_mask(1, 1, 0, 0x2UL, 0x1eUL, 1) == 0x2UL);
    /* Once uiClientSettings arrives, defer 100% to live scene client settings (orig_mask). */
    assert(zizhan_effective_pad_orient_mask(1, 0, 1, 0x2UL, 0x1eUL, 1) == 0x2UL);
    /* Scene-manifest iPad app with no Info.plist orientation keys: 0 -> 0x1e before client settings. */
    assert(zizhan_effective_pad_orient_mask(1, 0, 0, 0UL, 0UL, 0) == 0x1eUL);

    /* 9. Cold-launch placeholder & SBHomeGrabberView container orientation override (Preferences / FaceTime). */
    assert(zizhan_scene_launch_orient_override(1, 0, 1, 1, 4, 4, 1) == 4);
    assert(zizhan_scene_launch_orient_override(1, 0, 1, 0, 0, 4, 1) == 4);
    /* Live scene (current_live_orient != 0) or unsupported candidate keeps orig_orient untouched. */
    assert(zizhan_scene_launch_orient_override(1, 4, 1, 1, 4, 4, 1) == 1);
    assert(zizhan_scene_launch_orient_override(1, 0, 1, 1, 4, 4, 0) == 1);
    assert(zizhan_scene_launch_orient_override(0, 0, 1, 1, 4, 4, 1) == 1);

    /* 10. OUTER-04 folded outer-cover portrait lock & unfold restore state machine. */
    int was_closed = 0, saved_state = 0;
    /* Unfolded unlocked -> fold closed: locks to Portrait (1), records saved_state=1 */
    assert(zizhan_cover_orient_lock_step(1, 1, &was_closed, &saved_state, 0, 0) == 1);
    assert(was_closed == 1 && saved_state == 1);
    /* Staying folded while locked to 1: no redundant lock call */
    assert(zizhan_cover_orient_lock_step(1, 1, &was_closed, &saved_state, 1, 1) == 0);
    /* If lock drifted while folded, re-enforce Portrait (1) */
    assert(zizhan_cover_orient_lock_step(1, 1, &was_closed, &saved_state, 0, 0) == 1);
    /* Unfold: restores free IMU orientation (-1 = unlock) and clears saved_state */
    assert(zizhan_cover_orient_lock_step(1, 0, &was_closed, &saved_state, 1, 1) == -1);
    assert(was_closed == 0 && saved_state == 0);

    /* User manually locked to LandscapeRight (4) before folding -> fold locks to 1, unfold restores 14 (lock:4) */
    assert(zizhan_cover_orient_lock_step(1, 1, &was_closed, &saved_state, 1, 4) == 1);
    assert(saved_state == 14);
    assert(zizhan_cover_orient_lock_step(1, 0, &was_closed, &saved_state, 1, 1) == 14);
    assert(saved_state == 0);
    /* Subsequent unfolded polls remain idempotent (0) while user lock 4 stays active */
    assert(zizhan_cover_orient_lock_step(1, 0, &was_closed, &saved_state, 1, 4) == 0);

    /* Cold boot with phone already folded (was_closed=0, closed=1, SpringBoard initially unlocked):
     * immediately locks Portrait (1), and later unfolding restores free IMU rotation (-1). */
    was_closed = 0;
    saved_state = 0;
    assert(zizhan_cover_orient_lock_step(1, 1, &was_closed, &saved_state, 0, 0) == 1);
    assert(was_closed == 1 && saved_state == 1);
    assert(zizhan_cover_orient_lock_step(1, 0, &was_closed, &saved_state, 1, 1) == -1);
    assert(was_closed == 0 && saved_state == 0);

    printf("PASS scene-orient18\n");
    return 0;
}
