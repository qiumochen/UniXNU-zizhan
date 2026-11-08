# zizhan device repository

- This is the Xiaomi MIX Fold 2 (zizhan, SM8475) format 2 device repository, derived from UniXNU-pong and targeting iPadOS 18.7.10 / 22H374 / Darwin 24. Follow https://unixnu.com/zh/porting/device-repository and https://unixnu.com/zh/porting/records.
- Keep the ARM64, SM8475, OS profile and board layers separate. Preserve the community component identity PongPlatform24; zizhan changes stay in the device layer and name zizhan in comments and patch file names.
- zizhan values come from the phone's own live DT (`tools/gen_reserved.py`) or from the vendor sources. Do not reuse Pong or Sheng constants without checking them against zizhan.

# Probe policy

- Optional probes must record PASS, FAIL, SKIP or RUNNING and return to the normal boot path. Never end a boot with panic, halt or an unbounded loop to show a probe result.
- On failed prerequisites, log the reason and skip dependent MMIO/DMA work. Do not fabricate services, media or hardware resources to keep boot moving.
- Preserve real kernel panic handling. A failed optional probe does not authorize suppressing a genuine fatal error.

# Device trials

- Rear USB-A is the verified unattended path; do not substitute direct USB-C without a separate transport qualification.
- Use temporary `fastboot boot` only; do not flash partitions. Every boot argument set must include a way back (pong_autoreset, panic reset, power-key reset), otherwise the phone stays in iOS until someone unplugs it.
- Keep a full partition backup before any write to the phone. The root image is a pinned file on /data; never move or defragment it from Android.
- Keep the kernel command line under 1024 bytes (DarwinBoot24 refuses longer lines).

# Publishing

- Publish source, configuration, porting records and scripts only. Apple restore binaries, firmware, userdata, root images, credentials and build artifacts stay local.
- Record host build results separately from device results in porting.toml. A successful build is not evidence of a working device function.
- Scripts read paths from ZIZHAN_SRC; do not commit personal paths, host names or account names.

# Development handoff

- Read [current status](docs/progress/STATUS.md) first for the active baseline, task IDs, next experiment and verification boundaries. Read [CLAUDE.md](CLAUDE.md) for setup, build/test workflow and historical lessons. Dated notes preserve experiment history; they are not an independent current task queue.

# Progress management

- Define each active work item by its intended outcome, smallest useful deliverable, acceptance criteria, evidence, and next implementation action. Keep the long-term goal separate from the next deliverable.
- For feature work, prioritize the smallest safe end-to-end increment that exercises the real user or system path. For research, diagnostics, and infrastructure work, define a concrete artifact or decision that unblocks such an increment.
- Begin implementation when the inputs needed for one safe, bounded increment are known. Limit preparation to concrete implementation questions and identified safety blockers; do not require a complete architecture or subsystem audit before starting useful work.
- Each batch should produce a runnable or testable increment, or a specific finding that changes the next implementation decision. Builds, host models, read-only inventories, and documentation are supporting evidence unless they are the stated deliverable; do not report them as proof of a feature they do not exercise.
- Keep the next increment narrow and defer unrelated subsystems and broad qualification work. Follow all existing safety, rollback, and attended-test requirements; outcome-first delivery does not authorize bypassing them.
- When a concrete blocker prevents an action, state the exact unresolved condition, the smallest bounded check or implementation needed to resolve it, and the independent work that can continue meanwhile. Do not repeat an unchanged check without a stated control or reproduction purpose.
- After each batch, report what was delivered, the evidence level, what remains incomplete, and the single next action. Distinguish source changes, host-verified results, prepared candidates, installed state, and device-verified behavior.
- Maintain one current public ledger: `docs/progress/STATUS.md`. Link it from the progress index and CLAUDE.md. Keep local operational details in `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`; never publish that file or private inputs.
- At task entry, inspect the working tree and HEAD, read the ledger and relevant evidence, and check for an existing device cycle, installer or waiter before starting one. Reconcile any interrupted run before another device write. Do not equate a prepared artifact, an installed OS and the currently booted kernel.
- Give each work item a stable ID, priority, state, acceptance criteria, evidence and a concrete next action. Use `TODO`, `IN_PROGRESS`, `BLOCKED`, `HOST_VERIFIED`, `DEVICE_VERIFIED`, `DONE` or `REJECTED`. Record build, device and CI results separately. `DONE` means the item's stated criteria are met, not that the whole subsystem is qualified. Documentation tasks may finish with document checks; a hardware task cannot finish on host tests alone.
- Keep one main experiment in progress. Independent host checks or bounded regression runs may overlap only when they do not contend for phone access or mutable build/install state. Record any prerequisite before moving an item to `BLOCKED`, and proceed with authorized independent work.
- Before a device experiment, record its hypothesis, control, one intended variable, source revision and dirty state, kernel/image/UI/helper identity, relevant switches, deadline/return path, acceptance criteria and rollback. Identify local artifacts by manifest/hash; never assume a run label proves identical inputs.
- After each device run, material result, rejected candidate, baseline promotion, commit/push or CI conclusion, update the ledger and dated evidence before starting the next dependent experiment. Preserve failed/negative controls and explain what they rule out. Mark superseded interpretations explicitly; do not silently turn an old failure into a pass.
- Bound unattended runs by a deadline, log destination and verified return path; record their supervisor and cleanup action in the local handoff. Repeating an unchanged failure requires a stated control/reproduction purpose. Otherwise preserve the first failure and change the hypothesis or proceed to an independent authorized item.
- A run must distinguish `PASS`, `FAIL`, `SKIP` and `RUNNING`. Missing logs, incomplete screenshots, no boot, a timed-out waiter or an action merely dispatched are not `PASS`. First association, requested renewal, natural T1, same-AP reconnect and distinct-BSSID/SSID/subnet transitions are separate qualifications.
- Performance claims require matched scenarios and comparable inputs, sample count and median/p90 where available, visible content verification and explicit instrumentation overhead. Panel refresh rate is not app FPS. Use live hardware completion to diagnose GPU progress; old wait snapshots alone cannot prove a stall.
- Keep `porting.toml` findings, limits, next actions and evidence aligned when capability or code ownership changes. Public evidence uses full source revisions, reproducible commands and sanitized relative references. Keep the work queue in the ledger; do not duplicate it across historical notes.
- Before publishing, review the full diff for private data and run checks appropriate to the change. Driver/kernel changes require both reduced CI and full default kernel builds plus affected host regressions. Follow all triggered workflows on the exact pushed SHA; record pending/failure honestly and fix authorized failures. Do not reuse a green run from another SHA. Docs-only work does not require a local kernel rebuild.
- At interruption or handoff, update both the public ledger and local handoff: last verified baseline; accepted/rejected changes; unresolved limits; dirty files and commit/push/CI state; prepared versus installed artifacts; device state with observation time; owned processes/waiters; rollback; exact next action and stop conditions. Mark unobserved current state as unknown. A detached process is not durable unattended progress without verified supervision.
- A new session must resume the recorded next action and recheck live prerequisites. It must not repeat completed tests without a changed input or a stated regression reason, revive a rejected candidate without new evidence, or restart development solely because a historical paragraph says it is pending.
