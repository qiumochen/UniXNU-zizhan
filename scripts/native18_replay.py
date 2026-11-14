#!/usr/bin/env python3
"""Bounded host execution of actual kernel instructions for API analysis.

This models computational effects only. PAC instructions preserve raw
pointers and authenticated transfers use the actual register target; no key,
authentication, EL1, interrupt or MMIO behavior is qualified. Only writes to
an explicit fixture region are accepted. Unknown instructions remain real
Unicorn errors; no unknown helper or function is replaced by success.
"""
import struct


class KernelReplay:
    fixture_base = 0x5000000
    fixture_bytes = 0x100000
    stack = 0x50f0000
    return_address = 0x1000000

    def __init__(self, image, instruction_limit=200000):
        import unicorn
        from unicorn import arm64_const as reg
        self.unicorn, self.reg = unicorn, reg
        self.registers = [getattr(reg, 'UC_ARM64_REG_X' + str(index)) for index in range(31)]
        self.uc = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
        self.limit = instruction_limit
        self.count = 0
        self.executed = set()
        self.transfers = set()
        self.authentication_abstractions = set()
        self.trace = []
        self.image = image
        ranges = sorted((row['va'] & ~4095, (row['va'] + row['size'] + 4095) & ~4095)
                        for row in image.segments if row['size'])
        merged = []
        for first, last in ranges:
            if merged and first <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], last)
            else:
                merged.append([first, last])
        self.image_ranges = merged
        for first, last in merged:
            self.uc.mem_map(first, last - first)
        for row in image.segments:
            if row['filesize']:
                self.uc.mem_write(row['va'], image.read(row['va'], row['filesize']))
        self.uc.mem_map(self.return_address, 4096)
        self.uc.mem_map(self.fixture_base, self.fixture_bytes)
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.instruction)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write)

    def instruction(self, uc, pc, size, _):
        self.count += 1
        self.executed.add(pc)
        self.trace.append(pc)
        if len(self.trace) > 16:
            self.trace.pop(0)
        if self.count >= self.limit:
            raise RuntimeError('Actual API instruction path exceeds bounded host replay')
        word, = struct.unpack('<I', uc.mem_read(pc, 4))
        if word & 0xfffffc00 in (0xdac10000, 0xdac10400, 0xdac10800, 0xdac10c00,
                                 0xdac11000, 0xdac11400, 0xdac11800, 0xdac11c00):
            self.authentication_abstractions.add(word & 0xfffffc00)
            uc.reg_write(self.reg.UC_ARM64_REG_PC, pc + 4)
        elif word & 0xffffffe0 in (0xdac143e0, 0xdac147e0):
            self.authentication_abstractions.add(word & 0xffffffe0)
            uc.reg_write(self.reg.UC_ARM64_REG_PC, pc + 4)
        elif word in (0xd503237f, 0xd503233f, 0xd50323bf, 0xd50323ff):
            self.authentication_abstractions.add(word)
            uc.reg_write(self.reg.UC_ARM64_REG_PC, pc + 4)
        elif word in (0xd65f0fff, 0xd65f0bff):
            self.authentication_abstractions.add(word)
            uc.reg_write(self.reg.UC_ARM64_REG_PC, uc.reg_read(self.reg.UC_ARM64_REG_X30))
        elif word & 0xfffffc00 in (0xd73f0800, 0xd73f0c00, 0xd71f0800, 0xd71f0c00):
            target = uc.reg_read(self.registers[(word >> 5) & 31])
            if not any(first <= target < last for first, last in self.image_ranges):
                raise RuntimeError('Authenticated callback abstraction targets outside the actual image')
            self.transfers.add((pc, target))
            self.authentication_abstractions.add(word & 0xfffffc00)
            if word & 0xfffffc00 in (0xd73f0800, 0xd73f0c00):
                uc.reg_write(self.reg.UC_ARM64_REG_X30, pc + 4)
            uc.reg_write(self.reg.UC_ARM64_REG_PC, target)

    def write(self, uc, access, address, size, value, _):
        if not self.fixture_base <= address or address + size > self.fixture_base + self.fixture_bytes:
            raise RuntimeError('Actual API writes outside explicit stack/output fixture')

    def run(self, address, arguments, initial_registers=None):
        for register in self.registers:
            self.uc.reg_write(register, 0)
        self.uc.reg_write(self.reg.UC_ARM64_REG_X30, self.return_address)
        self.uc.reg_write(self.reg.UC_ARM64_REG_SP, self.stack)
        for index, value in (initial_registers or {}).items():
            self.uc.reg_write(self.registers[index], value)
        for index, value in enumerate(arguments):
            self.uc.reg_write(self.registers[index], value & ((1 << 64) - 1))
        self.count, self.trace = 0, []
        self.uc.emu_start(address, self.return_address, count=self.limit)
        if self.uc.reg_read(self.reg.UC_ARM64_REG_PC) != self.return_address:
            raise RuntimeError('Actual API did not return within bounded instruction count')
        return self.uc.reg_read(self.reg.UC_ARM64_REG_X0)
