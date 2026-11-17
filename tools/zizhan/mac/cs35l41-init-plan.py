#!/usr/bin/env python3
"""Extract actual-phone B2/OTP tables and plan volatile trim updates offline.

No device access, test-key executor, OTP programming or output enable exists.
The module's relocations, not fixed private offsets, select each OTP map.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

REPO = Path(__file__).resolve().parents[3]
TRIM_REGISTERS = {
    0x2030, 0x208c, 0x2090, 0x300c, 0x394c, 0x3950, 0x3954, 0x3958, 0x395c,
    0x4000, 0x400c, 0x410c, 0x4160, 0x416c, 0x4170, 0x4360, 0x4448, 0x444c,
    *range(0x6e30, 0x6e68, 4), 0x7068, 0x7418, 0x741c, 0x7434, 0x17040, 0x17044,
}
B2_PATCH = [(0x40, 0x5555), (0x40, 0xaaaa), (0x4100, 0), (0x4310, 0), (0x4400, 0),
            (0x381c, 0x51), (0x2bc20e0, 0), (0x2bc2020, 0), (0x40, 0xcccc), (0x40, 0x3333)]


def extent(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError("ELF extent outside file/section")
    return data[offset:offset + size]


def string(data, offset):
    if not 0 <= offset < len(data):
        raise ValueError("ELF string offset")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("unterminated ELF string")
    return data[offset:end].decode("ascii")


class Module:
    def __init__(self, data):
        if len(data) < 64 or len(data) > 16 * 1024 * 1024 or data[:7] != b"\x7fELF\x02\x01\x01":
            raise ValueError("need bounded little-endian ELF64")
        if struct.unpack_from("<HH", data, 16) != (1, 183):
            raise ValueError("need AArch64 relocatable module")
        at = struct.unpack_from("<Q", data, 40)[0]
        size, count, name_index = struct.unpack_from("<HHH", data, 58)
        if size != 64 or not 0 < count <= 512 or name_index >= count:
            raise ValueError("unreviewed ELF section layout")
        headers = extent(data, at, size * count)
        self.sections = [struct.unpack_from("<IIQQQQIIQQ", headers, i * 64) for i in range(count)]
        self.data = data
        names = self.section(name_index)
        self.names = [string(names, h[0]) for h in self.sections]
        tabs = [i for i, h in enumerate(self.sections) if h[1] == 2]
        if len(tabs) != 1:
            raise ValueError("need one ELF symbol table")
        self.symtab = tabs[0]
        tab = self.sections[self.symtab]
        if tab[9] != 24 or tab[5] % 24 or tab[5] > 24 * 65536 or tab[6] >= count:
            raise ValueError("bad symbol table layout")
        symnames = self.section(tab[6])
        symbols = self.section(self.symtab)
        self.symbols = []
        for i in range(0, len(symbols), 24):
            name, info, other, section, value, length = struct.unpack_from("<IBBHQQ", symbols, i)
            self.symbols.append(dict(name=string(symnames, name), section=section, value=value, size=length))

    def section(self, index):
        if index >= len(self.sections):
            raise ValueError("section index outside table")
        h = self.sections[index]
        if h[1] == 8:
            raise ValueError("NOBITS cannot contain source tables")
        return extent(self.data, h[4], h[5])

    def symbol(self, name):
        rows = [s for s in self.symbols if s["name"] == name]
        if len(rows) != 1:
            raise ValueError("missing/duplicate module symbol: " + name)
        return rows[0]

    def bytes(self, symbol):
        if symbol["section"] >= len(self.sections) or self.names[symbol["section"]] != ".rodata":
            raise ValueError("table is not immutable module rodata")
        return extent(self.section(symbol["section"]), symbol["value"], symbol["size"])

    def pointer(self, section, offset):
        found = []
        for i, h in enumerate(self.sections):
            # Relocation section NAME is deliberately not assumed; the phone
            # linker named it after a merged input, while sh_info targets rodata.
            if h[1] != 4 or h[7] != section:
                continue
            if h[6] != self.symtab or h[9] != 24 or h[5] % 24:
                raise ValueError("bad relocation table")
            raw = self.section(i)
            for at in range(0, len(raw), 24):
                where, info, addend = struct.unpack_from("<QQq", raw, at)
                if where != offset:
                    continue
                if info & 0xffffffff != 257 or info >> 32 >= len(self.symbols) or addend:
                    raise ValueError("need unique ABS64 OTP map symbol with zero addend")
                found.append(self.symbols[info >> 32])
        if len(found) != 1:
            raise ValueError("missing/duplicate OTP pointer relocation")
        return found[0]


def fields(rows, start_word, start_bit):
    if not isinstance(rows, list) or not rows or len(rows) > 128 or not 0 <= start_word < 32 or not 0 <= start_bit < 32:
        raise ValueError("invalid OTP table shape/start")
    cursor = start_word * 32 + start_bit
    used, result = {}, []
    for row in rows:
        reg, shift, size = row["register"], row["shift"], row["bits"]
        if any(type(x) is not int for x in (reg, shift, size)) or not 0 <= shift < 32 or not 0 <= size <= 32 or shift + size > 32:
            raise ValueError("OTP destination field invalid")
        if cursor + size > 1024 or (not size and reg):
            raise ValueError("OTP source range/zero-width destination invalid")
        mask = ((1 << size) - 1) << shift
        if reg:
            if reg not in TRIM_REGISTERS or mask & used.get(reg, 0):
                raise ValueError("unreviewed/overlapping volatile trim destination")
            used[reg] = used.get(reg, 0) | mask
        result.append(dict(register=reg, mask=mask, shift=shift, bits=size, source_bit=cursor,
                           reserved=reg == 0))
        cursor += size
    return result, cursor


def decode(words, rows, start_word=2, start_bit=16):
    if not isinstance(words, list) or len(words) != 32 or any(type(w) is not int or not 0 <= w <= 0xffffffff for w in words):
        raise ValueError("need exactly32 unsigned host-order OTP words")
    schema, end = fields(rows, start_word, start_bit)
    source = sum(w << (32 * i) for i, w in enumerate(words))
    result = []
    for f in schema:
        if f["reserved"]:
            continue
        value = ((source >> f["source_bit"]) & ((1 << f["bits"]) - 1)) << f["shift"]
        result.append(dict(register=f["register"], mask=f["mask"], value=value, operation="volatile_update_bits"))
    return result


def extract(data):
    module = Module(data)
    symbol = module.symbol("cs35l41_otp_map_map")
    raw = module.bytes(symbol)
    if len(raw) != 120:
        raise ValueError("unreviewed five-entry OTP table ABI")
    maps = []
    for at in range(0, 120, 24):
        identity, count, pointer, bit, word = struct.unpack_from("<IIQII", raw, at)
        target = module.pointer(symbol["section"], symbol["value"] + at + 8)
        if pointer or count != 100 or target["size"] != count * 8 or (word, bit) != (2, 16):
            raise ValueError("unreviewed OTP pointer/count/start ABI")
        if not (target["name"].startswith("otp_map_1.") or target["name"].startswith("otp_map_2.")):
            raise ValueError("unexpected OTP table target")
        table = module.bytes(target)
        rows = []
        for offset in range(0, len(table), 8):
            reg, shift, size, padding = struct.unpack_from("<IBBH", table, offset)
            if padding:
                raise ValueError("OTP map padding is not zero")
            rows.append(dict(register=reg, shift=shift, bits=size))
        schema, end = fields(rows, word, bit)
        if end != 768:
            raise ValueError("unreviewed OTP consumed-bit count")
        maps.append(dict(id=identity, source_symbol=target["name"], start_word=word, start_bit=bit,
                         consumed_bits=end - word * 32 - bit, fields=schema, table_rows=rows))
    if [m["id"] for m in maps] != [1, 2, 3, 6, 8]:
        raise ValueError("unreviewed OTP map IDs/order")
    patch = module.bytes(module.symbol("cs35l41_revb2_errata_patch"))
    if len(patch) != 120:
        raise ValueError("unexpected B2 patch length")
    triples = list(struct.iter_unpack("<III", patch))
    if triples != [(reg, value, 0) for reg, value in B2_PATCH]:
        raise ValueError("B2 patch does not match reviewed actual-phone table")
    return dict(otp_maps=maps, b2_errata=[dict(register=r, value=v) for r, v in B2_PATCH],
                otp_read_recipe=dict(id_register=0x10, first_word_register=0x400, words=32, register_stride=4,
                                     byte_order="BE32 on wire; host unsigned words in snapshot"),
                phases=["reset and actual B2 identity/OTP boot check", "output-disabled/DSP-stopped readback",
                        "actual B2 errata guarded test-key transaction", "read own OTP_ID and32 words",
                        "select own map, masked volatile trim updates, readback and relock",
                        "leave output disabled; reset on failure"],
                executor_prerequisites=["fresh per-chip OTP_ID and32-word snapshot",
                                        "actual output-disabled and HALO-stopped register readback",
                                        "separate reviewed B2-errata and OTP-trim test-key handling",
                                        "bounded volatile update/readback/relock and failure reset"],
                snapshot_required=True, plan_executable=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--module", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(REPO) or output.exists() or args.module.resolve().is_relative_to(REPO):
        raise SystemExit("module and new report must stay private")
    report = dict(status="FAIL", created=datetime.now(timezone.utc).isoformat(),
                  scope="offline-actual-module-otp-schema-only", phone_operations=False, hardware_writes=False,
                  otp_programmed=False, otp_snapshot_read=False, otp_unpacked=False,
                  dsp_started=False, audio_output=False,
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    try:
        if args.module.is_symlink() or args.manifest.is_symlink() or args.module.stat().st_size > 16 * 1024 * 1024 or args.manifest.stat().st_size > 1024 * 1024:
            raise ValueError("private input type/size rejected")
        data = args.module.read_bytes()
        manifest = json.loads(args.manifest.read_text())
        digest = hashlib.sha256(data).hexdigest()
        if manifest["sha256"] != digest or manifest["bytes"] != len(data) or manifest["origin"] != "/vendor_dlkm/lib/modules/cs35l41_dlkm.ko":
            raise ValueError("actual module input identity mismatch")
        report.update(extract(data))
        report["module_sha256"] = digest
        report["status"] = "HOST_VERIFIED_B2_OTP_SCHEMA"
    except (ValueError, KeyError, TypeError, OSError, UnicodeError, struct.error) as error:
        report["error"] = str(error)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as f:
        json.dump(report, f, indent=2)
        f.write("\n")
    print(report["status"])
    return 0 if report["status"] == "HOST_VERIFIED_B2_OTP_SCHEMA" else 1


if __name__ == "__main__":
    raise SystemExit(main())
