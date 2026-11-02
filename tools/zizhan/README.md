# zizhan tools

| Path | Runs on | Purpose |
| --- | --- | --- |
| `gen_reserved.py` | any | Print the memory and no-map reserved-memory regions of the phone's decompiled live `/sys/firmware/fdt`, the source of `dts/zizhan.dts` |
| `mdt2mbn.py` | any | Merge a split `*.mdt` + `*.bNN` firmware into one `.mbn` (ADSP) |
| `phone/collect.sh` | phone (root) | Copy the live DT, firmware lists, Adreno firmware and bus/display/battery facts to `/data/local/tmp/collect.tgz` |
| `phone/parts.sh` | phone (root) | List partitions with their block devices and sizes, and the UFS LUNs |
| `phone/pinalloc.c` | phone (root) | Create a pinned, fully allocated f2fs file and print its physical extents (the root image); freestanding aarch64 binary |
| `mac/` | Mac | The build and test pipeline; see the table in the top-level README |

The Mac scripts read `ZIZHAN_SRC` (default `~/zizhan`) and source `mac/ci-env.sh` from there, so copy `mac/*` into `$ZIZHAN_SRC` or set `ZIZHAN_SRC` and run them from this directory. They need Homebrew (`/opt/homebrew/bin`), `adb`/`fastboot`, `mtools` and, for `usbkick.sh`, `uhubctl`.

`ZIZHAN_ASK_FILE` is optional: when set, `boot.sh` and `cycle.sh` write a one-line request there (shown by a phone-side photo page during unattended runs).

`mac/job-restore.sh TAG FROMTAG LABEL…` restores jobs from `prepared/pong-ipados18-system.FROMTAG.cdr`, saves the current image/layout under TAG, recalculates the layout and installs only the OS window. It requires the same backed-up, pinned-root Android setup as `jobs-refresh.sh`. It does not change the full-rebuild exclusion policy. Mount/edit/layout failures stop installation; inspect the local backup before retrying. The generalized script has only had syntax checks, not a Mac/device run.

The old `mac-tree.sh` Linux-to-Mac mirror is retired: the Mac edits and builds directly from its clone. Use normal git commits and `git pull --ff-only` between hosts; see [CLAUDE.md](../../CLAUDE.md). No host alias or remote workspace is needed.
