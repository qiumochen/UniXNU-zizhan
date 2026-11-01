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
