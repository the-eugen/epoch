#!/usr/bin/env python3
"""Generate LDA instruction tests for the MOS6502 emulator."""
from __future__ import annotations
from pathlib import Path
from textwrap import dedent
from dataclasses import dataclass, field
from enum import Enum, IntFlag, auto
from typing import Union, TypeAlias, Literal, Callable

class StatusFlags(IntFlag):
    N = 0x80
    Z = 0x02

class Register(Enum):
    A = "A"
    X = "X"
    Y = "Y"

class AddressModeId(Enum):
    Immediate   = "imm"
    Zeropage    = "zpage"
    ZeropageX   = "zpagex"
    ZeropageY   = "zpagey"
    Absolute    = "abs"
    AbsoluteX   = "absx"
    AbsoluteY   = "absy"
    IndirectX   = "indx"
    IndirectY   = "indy"

@dataclass
class CodeTemplate:
    segs: list[tuple[int, bytes]]
    tag: Optional[str] = None
    state: Optional[dict[Register, byte]] = None
    eaddr: Optional[int] = None
    xpage: Optional[bool] = False

HLT_MARKER: byte = 0x02
TemplateGen: TypeAlias = Callable[[int, int], list[CodeTemplate]]

# Test code templates for load-type instruction
load_templates: dict[AddressModeId, TemplateGen] = {
    AddressModeId.Immediate: lambda op, val: [
        CodeTemplate(
            segs    = [(0x0000, [op, val, HLT_MARKER])],
        ),
    ],
    AddressModeId.Zeropage: lambda op, val: [
        CodeTemplate(
            segs    = [(0x0000, [op, 0x03, HLT_MARKER, val])],
            eaddr   = 0x0003,
        ),
    ],
    AddressModeId.ZeropageX: lambda op, val: [
        CodeTemplate(
            state   = {Register.X: 0x01},
            segs    = [(0x0000, [op, 0x02, HLT_MARKER, val])],
            eaddr   = 0x0003,
        ),
        CodeTemplate(
            state   = {Register.X: 0x04},
            segs    = [(0x0000, [op, 0xff, HLT_MARKER, val])],
            tag     = "overflow",
            eaddr   = 0x0003,
        ),
    ],
    AddressModeId.ZeropageY: lambda op, val: [
        CodeTemplate(
            state   = {Register.Y: 0x01},
            segs    = [(0x0000, [op, 0x02, HLT_MARKER, val])],
            eaddr   = 0x0003,
        ),
        CodeTemplate(
            state   = {Register.Y: 0x04},
            segs    = [(0x0000, [op, 0xff, HLT_MARKER, val])],
            tag     = "overflow",
            eaddr   = 0x0003,
        ),
    ],
    AddressModeId.Absolute: lambda op, val: [
        CodeTemplate(
            segs    = [(0x0000, [op, 0x01, 0x10, HLT_MARKER]), (0x1001, [val])],
            eaddr   = 0x1001,
        ),
    ],
    AddressModeId.AbsoluteX: lambda op, val: [
        CodeTemplate(
            state   = {Register.X: 0x01},
            segs    = [(0x0000, [op, 0x00, 0x10, HLT_MARKER]), (0x1001, [val])],
            eaddr   = 0x1001,
        ),
        CodeTemplate(
            state   = {Register.X: 0x02},
            segs    = [(0x0000, [op, 0xFF, 0x0F, HLT_MARKER]), (0x1001, [val])],
            tag     = "xpage",
            eaddr   = 0x1001,
            xpage   = True,
        ),
    ],
    AddressModeId.AbsoluteY: lambda op, val: [
        CodeTemplate(
            state   = {Register.Y: 0x01},
            segs    = [(0x0000, [op, 0x00, 0x10, HLT_MARKER]), (0x1001, [val])],
            eaddr   = 0x1001,
        ),
        CodeTemplate(
            state   = {Register.Y:0x02},
            segs    = [(0x0000, [op, 0xFF, 0x0F, HLT_MARKER]), (0x1001, [val])],
            tag     = "xpage",
            eaddr   = 0x1001,
            xpage   = True,
        ),
    ],
    AddressModeId.IndirectX: lambda op, val: [
        CodeTemplate(
            state   = {Register.X: 0x01},
            segs    = [(0x0000, [op, 0x02, HLT_MARKER, 0x80]), (0x0080, [val])],
            eaddr   = 0x0080,
        ),
        CodeTemplate(
            state   = {Register.X: 0x04},
            segs    = [(0x0000, [op, 0xFF, HLT_MARKER, 0x80]), (0x0080, [val])],
            tag     = "overflow",
            eaddr   = 0x0080,
        ),
    ],
    AddressModeId.IndirectY: lambda op, val: [
        CodeTemplate(
            state   = {Register.Y: 0x04},
            segs    = [(0x0000, [op, 0x03, HLT_MARKER, 0x80, 0x10]), (0x1084, [val])],
            eaddr   = 0x1084,
        ),
        CodeTemplate(
            state   = {Register.Y: 0x80},
            segs    = [(0x0000, [op, 0x03, HLT_MARKER, 0x80, 0x10]), (0x1100, [val])],
            tag     = "xpage",
            eaddr   = 0x1100,
            xpage   = True,
        ),
    ],
}

Operand: TypeAlias = Union[Register, Literal['Memory']]

@dataclass
class Instruction:
    mnemonic:   str
    modes:      dict[AddressModeId, tuple[byte, int]] # mode -> (opcode, timing)
    templates:  dict[AddressModeId, TemplateGen]
    values:     list[bytes]
    flags:      Optional[int] = 0
    operands:   Optional[list[Operand]] = field(default_factory=list)
    dest:       Optional[Operand] = None
    xpagestall: Optional[bool] = False

def gen_instruction_tests(instr: Instruction) -> None:
    for mode, (opcode, timing) in instr.modes.items():
        gen_templates = instr.templates[mode]
        for value in instr.values:
            for template in gen_templates(opcode, value):
                template_tag = f"_{template.tag}" if template.tag else ""
                test_name = f"test_{instr.mnemonic}_{mode.value}{template_tag}_{value:02x}".lower()

                print(f"ep_test({test_name})")
                print( "{")
                print( "    const struct test_ram_segment segments[] = {")
                for base, data in template.segs:
                    hex_bytes = ", ".join(f"0x{b:02x}" for b in data)
                    print(f"        MAKE_TEST_SEGMENT_VEC(0x{base:04x}, {{{hex_bytes}}}),")
                print( "    };")
                print()

                print( "    struct mos6502_cpu cpu;")
                print(f"    init_test_cpu(&cpu, segments, {len(template.segs)});");
                print()

                # Setup the src operands and template state
                for op in instr.operands:
                    if op == Register.X or op == Register.Y or op == Register.A:
                        print(f"    cpu.{op.value} = 0x{value:02x};");
                    else:
                        raise ValueError("Invalid operand type")
                if template.state:
                    for reg, val in template.state.items():
                        print(f"    cpu.{reg.value} = 0x{val:02x};");

                print(f"    mos_word_t orig_flags = cpu.P;");
                print(f"    uint64_t cycles = run_test_cpu(&cpu) - 1 /* Subtract 1 cycle for HLT */;");
                print()

                if instr.xpagestall and template.xpage:
                    print(f"    ep_verify_equal(cycles, {timing} + 1);")
                else:
                    print(f"    ep_verify_equal(cycles, {timing});")

                # Validate the instr output
                if instr.dest:
                    if instr.dest == Register.X or instr.dest == Register.Y or instr.dest == Register.A:
                        print(f"    ep_verify_equal(cpu.{instr.dest.value}, 0x{value:02x});")
                    elif instr.dest == 'Memory':
                        print(f"    ep_verify_equal(mos6502_load_word(&cpu, 0x{template.eaddr:04x}), 0x{value:02x});")
                    else:
                        raise ValueError("Invalid output operand type")
                print()

                # Check that flags affected by the instruction are set and that the inverse is true as well.
                affected_flags: str = "0x00";
                if instr.flags & StatusFlags.Z and value == 0:
                    affected_flags += " | SR_Z";
                if instr.flags & StatusFlags.N and value & 0x80:
                    affected_flags += " | SR_N";
                print(f"    mos_word_t affected_flags = {affected_flags};");
                print(f"    ep_verify_equal(cpu.P & affected_flags, affected_flags);");
                print(f"    ep_verify_equal(cpu.P & ~affected_flags, orig_flags & ~affected_flags);");
                print()

                print( "    free_test_cpu(&cpu);");
                print( "}\n")

instructions: list[Instruction] = [
    Instruction(
        mnemonic    = 'LDX',
        modes       = {
                        AddressModeId.Immediate:    (0xA2, 2),
                        AddressModeId.Zeropage:     (0xA6, 3),
                        AddressModeId.ZeropageY:    (0xB6, 4),
                        AddressModeId.Absolute:     (0xAE, 4),
                        AddressModeId.AbsoluteY:    (0xBE, 4),
                    },
        flags       = StatusFlags.N | StatusFlags.Z,
        templates   = load_templates,
        values      = [0x42, 0xAA, 0x00],
        dest        = Register.X,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'LDY',
        modes       = {
                        AddressModeId.Immediate:    (0xA0, 2),
                        AddressModeId.Zeropage:     (0xA4, 3),
                        AddressModeId.ZeropageX:    (0xB4, 4),
                        AddressModeId.Absolute:     (0xAC, 4),
                        AddressModeId.AbsoluteX:    (0xBC, 4),
                    },
        flags       = StatusFlags.N | StatusFlags.Z,
        templates   = load_templates,
        values      = [0x42, 0xAA, 0x00],
        dest        = Register.Y,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'LDA',
        modes       = {
                        AddressModeId.Immediate:    (0xA9, 2),
                        AddressModeId.Zeropage:     (0xA5, 3),
                        AddressModeId.ZeropageX:    (0xB5, 4),
                        AddressModeId.Absolute:     (0xAD, 4),
                        AddressModeId.AbsoluteX:    (0xBD, 4),
                        AddressModeId.AbsoluteY:    (0xB9, 4),
                        AddressModeId.IndirectX:    (0xA1, 6),
                        AddressModeId.IndirectY:    (0xB1, 5),
                    },
        flags       = StatusFlags.N | StatusFlags.Z,
        templates   = load_templates,
        values      = [0x42, 0xAA, 0x00],
        dest        = Register.A,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'STX',
        modes       = {
                        AddressModeId.Zeropage:     (0x86, 3),
                        AddressModeId.ZeropageY:    (0x96, 4),
                        AddressModeId.Absolute:     (0x8E, 4),
                    },
        templates   = load_templates,
        values      = [0x42],
        operands    = [Register.X],
        dest        = 'Memory',
    ),
    Instruction(
        mnemonic    = 'STY',
        modes       = {
                        AddressModeId.Zeropage:     (0x84, 3),
                        AddressModeId.ZeropageX:    (0x94, 4),
                        AddressModeId.Absolute:     (0x8C, 4),
                    },
        templates   = load_templates,
        values      = [0x42],
        operands    = [Register.Y],
        dest        = 'Memory',
    ),
    Instruction(
        mnemonic    = 'STA',
        modes       = {
                        AddressModeId.Zeropage:     (0x85, 3),
                        AddressModeId.ZeropageX:    (0x95, 4),
                        AddressModeId.Absolute:     (0x8D, 4),
                        AddressModeId.AbsoluteX:    (0x9D, 5),
                        AddressModeId.AbsoluteY:    (0x99, 5),
                        AddressModeId.IndirectX:    (0x81, 6),
                        AddressModeId.IndirectY:    (0x91, 6),
                    },
        templates   = load_templates,
        values      = [0x42],
        operands    = [Register.A],
        dest        = 'Memory',
    ),
]

print(f"/* This file is auto-generated from {Path(__file__).name} */\n");

for instr in instructions:
    gen_instruction_tests(instr)
