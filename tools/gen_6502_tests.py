#!/usr/bin/env python3
"""Generate LDA instruction tests for the MOS6502 emulator."""
from __future__ import annotations
from pathlib import Path
from textwrap import dedent
from dataclasses import dataclass, field
from enum import Enum, IntFlag, auto
from typing import Union, TypeAlias, Literal, Callable
from itertools import product

class StatusFlags(IntFlag):
    N = 0x80
    Z = 0x02
    C = 0x01
    V = 0x40

class Register(Enum):
    A = "A"
    X = "X"
    Y = "Y"
    P = "P"
    SP = "SP"

class AddressModeId(Enum):
    Implied     = "imp"
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

# Test code templates
templates: dict[AddressModeId, TemplateGen] = {
    AddressModeId.Implied: lambda op, val: [
        CodeTemplate(
            segs    = [(0x0000, [op, HLT_MARKER])],
        ),
    ],
    AddressModeId.Immediate: lambda op, val: [
        CodeTemplate(
            segs    = [(0x0000, [op, val, HLT_MARKER])],
            eaddr   = 0x0001,
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

Operand: TypeAlias = Union[Register, Literal['Memory'], Literal['Flags']]
Semantics = Callable[dict[Operand, int], dict[Operand, int]]

class TemplateDataStrat(Enum):
    FromTestcase = auto()
    InvertExpected = auto()
    Nop = auto()

@dataclass
class Instruction:
    mnemonic:   str
    modes:      dict[AddressModeId, tuple[byte, int]] # mode -> (opcode, timing)
    testcases:  dict[Operand, [int]]
    semantics:  Semantics
    tdatastrat: Optional[TemplateDataStrat] = TemplateDataStrat.Nop
    xpagestall: Optional[bool] = False

# Used for load-type instructions where template data value is the testcase input
def data_value_from_testcase(tc, exp):
    return tc["Memory"]

# Used for store-type instructions where template data value is the inverse of expected to catch missing stores
def data_value_invert_expected(tc, exp):
    return (~exp["Memory"]) & 0xFF

# Used for instructions that do not touch memory (e.g. NOP)
def data_value_constant(tc, exp):
    return 0

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
        semantics   = lambda tc: {
                        Register.X: tc['Memory'],
                        'Flags':    (StatusFlags.Z if tc['Memory'] == 0 else 0) |
                                    (StatusFlags.N if tc['Memory'] & 0x80 else 0)
                      },
        testcases   = {'Memory': [0x42, 0xAA, 0x00]},
        tdatastrat  = TemplateDataStrat.FromTestcase,
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
        semantics   = lambda tc: {
                        Register.Y: tc['Memory'],
                        'Flags':    (StatusFlags.Z if tc['Memory'] == 0 else 0) |
                                    (StatusFlags.N if tc['Memory'] & 0x80 else 0)
                      },
        testcases   = {'Memory': [0x42, 0xAA, 0x00]},
        tdatastrat  = TemplateDataStrat.FromTestcase,
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
        semantics   = lambda tc: {
                        Register.A: tc['Memory'],
                        'Flags':    (StatusFlags.Z if tc['Memory'] == 0 else 0) |
                                    (StatusFlags.N if tc['Memory'] & 0x80 else 0)
                      },
        testcases   = {'Memory': [0x42, 0xAA, 0x00]},
        tdatastrat  = TemplateDataStrat.FromTestcase,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'STX',
        modes       = {
                        AddressModeId.Zeropage:     (0x86, 3),
                        AddressModeId.ZeropageY:    (0x96, 4),
                        AddressModeId.Absolute:     (0x8E, 4),
                      },
        semantics   = lambda tc: {
                        'Memory': tc[Register.X],
                      },
        testcases   = {Register.X: [0x42]},
        tdatastrat  = TemplateDataStrat.InvertExpected,
    ),
    Instruction(
        mnemonic    = 'STY',
        modes       = {
                        AddressModeId.Zeropage:     (0x84, 3),
                        AddressModeId.ZeropageX:    (0x94, 4),
                        AddressModeId.Absolute:     (0x8C, 4),
                      },
        semantics   = lambda tc: {
                        'Memory': tc[Register.Y],
                      },
        testcases   = {Register.Y: [0x42]},
        tdatastrat  = TemplateDataStrat.InvertExpected,
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
        semantics   = lambda tc: {
                        'Memory': tc[Register.A],
                      },
        testcases   = {Register.A: [0x42]},
        tdatastrat  = TemplateDataStrat.InvertExpected,
    ),
    Instruction(
        mnemonic    = 'TAX',
        modes       = {
                        AddressModeId.Implied:      (0xAA, 2)
                      },
        semantics   = lambda tc: {
                        Register.X: tc[Register.A],
                        'Flags':    (StatusFlags.Z if tc[Register.A] == 0 else 0) |
                                    (StatusFlags.N if tc[Register.A] & 0x80 else 0)
                      },
        testcases   = {Register.A: [0x00, 0xAA, 0x42]},
    ),
    Instruction(
        mnemonic    = 'TAY',
        modes       = {
                        AddressModeId.Implied:      (0xA8, 2)
                      },
        semantics   = lambda tc: {
                        Register.Y: tc[Register.A],
                        'Flags':    (StatusFlags.Z if tc[Register.A] == 0 else 0) |
                                    (StatusFlags.N if tc[Register.A] & 0x80 else 0)
                      },
        testcases   = {Register.A: [0x00, 0xAA, 0x42]},
    ),
    Instruction(
        mnemonic    = 'TSX',
        modes       = {
                        AddressModeId.Implied:      (0xBA, 2)
                      },
        semantics   = lambda tc: {
                        Register.X: tc[Register.SP],
                        'Flags':    (StatusFlags.Z if tc[Register.SP] == 0 else 0) |
                                    (StatusFlags.N if tc[Register.SP] & 0x80 else 0)
                      },
        testcases   = {Register.SP: [0x00, 0xAA, 0x42]},
    ),
    Instruction(
        mnemonic    = 'TXA',
        modes       = {
                        AddressModeId.Implied:      (0x8A, 2)
                      },
        semantics   = lambda tc: {
                        Register.A: tc[Register.X],
                        'Flags':    (StatusFlags.Z if tc[Register.X] == 0 else 0) |
                                    (StatusFlags.N if tc[Register.X] & 0x80 else 0)
                      },
        testcases   = {Register.X: [0x00, 0xAA, 0x42]},
    ),
    Instruction(
        mnemonic    = 'TXS',
        modes       = {
                        AddressModeId.Implied:      (0x9A, 2)
                      },
        semantics   = lambda tc: {
                        Register.SP: tc[Register.X],
                        'Flags':    (StatusFlags.Z if tc[Register.X] == 0 else 0) |
                                    (StatusFlags.N if tc[Register.X] & 0x80 else 0)
                      },
        testcases   = {Register.X: [0x00, 0xAA, 0x42]},
    ),
    Instruction(
        mnemonic    = 'TYA',
        modes       = {
                        AddressModeId.Implied:      (0x98, 2)
                      },
        semantics   = lambda tc: {
                        Register.A: tc[Register.Y],
                        'Flags':    (StatusFlags.Z if tc[Register.Y] == 0 else 0) |
                                    (StatusFlags.N if tc[Register.Y] & 0x80 else 0)
                      },
        testcases   = {Register.Y: [0x00, 0xAA, 0x42]},
    ),
    Instruction(
        mnemonic    = 'PHA',
        modes       = {
                        AddressModeId.Implied:      (0x48, 3)
                      },
        semantics   = lambda tc: {
                        Register.SP: tc[Register.SP] - 1,
                        'Stack':     tc[Register.A],
                      },
        testcases   = { Register.A: [0xAA], Register.SP: [0xFD] },
    ),
    Instruction(
        mnemonic    = 'PLA',
        modes       = {
                        AddressModeId.Implied:      (0x68, 4)
                      },
        semantics   = lambda tc: {
                        Register.A:     tc['Stack'],
                        Register.SP:    tc[Register.SP] + 1,
                        'Flags':        (StatusFlags.Z if tc['Stack'] == 0 else 0) |
                                        (StatusFlags.N if tc['Stack'] & 0x80 else 0)
                      },
        testcases   = { 'Stack': [0x00, 0xAA, 0x42], Register.SP: [0xFC] },
    ),
    Instruction(
        mnemonic    = 'PHP',
        modes       = {
                        AddressModeId.Implied:      (0x08, 3)
                      },
        semantics   = lambda tc: {
                        # SP is pushed on stack with bits 4 and 5 set
                        'Stack':     tc[Register.P] | 0x30,
                        Register.SP: tc[Register.SP] - 1,
                      },
        testcases   = { Register.P: [0xCF], Register.SP: [0xFD] },
    ),
    Instruction(
        mnemonic    = 'PLP',
        modes       = {
                        AddressModeId.Implied:      (0x28, 4)
                      },
        semantics   = lambda tc: {
                        # SP pulled from stack ignores bits 4 and 5
                        'Flags':     (tc['Stack'] & ~0x30) | (tc[Register.P] & 0x30),
                        Register.SP: tc[Register.SP] + 1,
                      },
        testcases   = { 'Stack': [0xFF], Register.P: [0x16], Register.SP: [0xFC] },
    ),
]

print(f"/* This file is auto-generated from {Path(__file__).name} */\n");

def expand_operand_values(spec: dict[Operand, list[int]]):
    keys = spec.keys()
    for p in product(*spec.values()):
        yield dict(zip(keys, p))

def gen_instruction_tests(instr: Instruction) -> None:
    for testcase in expand_operand_values(instr.testcases):
        expected = instr.semantics(testcase)
        for mode, (opcode, timing) in instr.modes.items():

            if instr.tdatastrat == TemplateDataStrat.FromTestcase:
                data_val = testcase['Memory']
            elif instr.tdatastrat == TemplateDataStrat.InvertExpected:
                data_val = ~expected['Memory'] & 0xFF
            elif instr.tdatastrat == TemplateDataStrat.Nop:
                data_val = 0

            gen_templates = templates[mode]
            for template in gen_templates(opcode, data_val):

                template_tag = f"_{template.tag}" if template.tag else ""
                testcase_tag = "_".join(f"{v:02x}" for v in testcase.values())
                test_name = f"test_{instr.mnemonic}_{mode.value}{template_tag}_{testcase_tag}".lower()

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
                for key, value in testcase.items():
                    if isinstance(key, Register):
                        print(f"    cpu.{key.value} = 0x{value:02x};");
                    elif key == 'Stack':
                        sp = testcase[Register.SP] + 1
                        print(f"    mos6502_store_word(&cpu, 0x0100 | 0x{sp:02x}, 0x{value:02x});")
                    elif key != 'Memory':
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

                # Validate the instr expected output
                affected_flags = 0
                for key, value in expected.items():
                    if isinstance(key, Register):
                        print(f"    ep_verify_equal(cpu.{key.value}, 0x{value:02x});")
                    elif key == 'Memory':
                        print(f"    ep_verify_equal(mos6502_load_word(&cpu, 0x{template.eaddr:04x}), 0x{value:02x});")
                    elif key == 'Flags':
                        affected_flags = f"0x{value:02x}"
                    elif key == 'Stack':
                        print(f"    ep_verify_equal(mos6502_load_word(&cpu, 0x0100 | (cpu.SP + 1)), 0x{value:02x});")
                    else:
                        raise ValueError("Invalid output operand type")

                # If there are no affected flags we still check for unexpected modifications
                if affected_flags != 0:
                    print(f"    ep_verify_equal(cpu.P & {affected_flags}, {affected_flags});");
                    print(f"    ep_verify_equal(cpu.P & ~{affected_flags}, orig_flags & ~{affected_flags});");
                else:
                    print(f"    ep_verify_equal(cpu.P, orig_flags);")
                print()

                print( "    free_test_cpu(&cpu);");
                print( "}\n")

for instr in instructions:
    gen_instruction_tests(instr)
