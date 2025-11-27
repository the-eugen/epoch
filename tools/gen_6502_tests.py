#!/usr/bin/env python3
"""Generate instruction tests for the MOS6502 emulator."""
from __future__ import annotations
from pathlib import Path
from textwrap import dedent
from dataclasses import dataclass, field
from enum import Enum, IntFlag, auto
from typing import Union, TypeAlias, Literal, Callable, ClassVar
from itertools import product

class StatusFlags(IntFlag):
    N = 0x80
    Z = 0x02
    C = 0x01
    V = 0x40
    I = 0x04

class Register(Enum):
    A = "A"
    X = "X"
    Y = "Y"
    P = "P"
    SP = "SP"
    PC = "PC"

class AddressModeId(Enum):
    Implied     = "imp"
    Immediate   = "imm"
    Zeropage    = "zpage"
    ZeropageX   = "zpagex"
    ZeropageY   = "zpagey"
    Absolute    = "abs"
    AbsoluteX   = "absx"
    AbsoluteY   = "absy"
    Indirect    = "ind" # Only used for jumps
    IndirectX   = "indx"
    IndirectY   = "indy"
    Relative    = "rel"

@dataclass
class ModeTemplate:
    apply:  callable[[int, dict], list[tuple[int, bytes]]]
    tag:    str = None
    state:  dict[Register, byte] = None
    eaddr:  int = None
    xpage:  bool = False

# Test code templates
default_templates: dict[AddressModeId, list[ModeTemplate]] = {
    AddressModeId.Implied: [
        ModeTemplate(
            apply   = lambda op, operands: [(0x0000, [op])],
        ),
    ],
    AddressModeId.Immediate: [
        ModeTemplate(
            apply   = lambda op, operands: [(0x0000, [op, operands['Memory']])],
            eaddr   = 0x0001,
        ),
    ],
    AddressModeId.Zeropage: [
        ModeTemplate(
            apply   = lambda op, operands: [(0x0000, [op, 0x02, operands['Memory']])],
            eaddr   = 0x0002,
        ),
    ],
    AddressModeId.ZeropageX: [
        ModeTemplate(
            state   = {Register.X: 0x01},
            apply   = lambda op, operands: [(0x0000, [op, 0x01, operands['Memory']])],
            eaddr   = 0x0002,
        ),
        ModeTemplate(
            state   = {Register.X: 0x03},
            apply   = lambda op, operands: [(0x0000, [op, 0xff, operands['Memory']])],
            tag     = "overflow",
            eaddr   = 0x0002,
        ),
    ],
    AddressModeId.ZeropageY: [
        ModeTemplate(
            state   = {Register.Y: 0x01},
            apply   = lambda op, operands: [(0x0000, [op, 0x01, operands['Memory']])],
            eaddr   = 0x0002,
        ),
        ModeTemplate(
            state   = {Register.Y: 0x03},
            apply   = lambda op, operands: [(0x0000, [op, 0xff, operands['Memory']])],
            tag     = "overflow",
            eaddr   = 0x0002,
        ),
    ],
    AddressModeId.Absolute: [
        ModeTemplate(
            apply   = lambda op, operands: [(0x0000, [op, 0x01, 0x10]), (0x1001, [operands['Memory']])],
            eaddr   = 0x1001,
        ),
    ],
    AddressModeId.AbsoluteX: [
        ModeTemplate(
            state   = {Register.X: 0x01},
            apply   = lambda op, operands: [(0x0000, [op, 0x00, 0x10]), (0x1001, [operands['Memory']])],
            eaddr   = 0x1001,
        ),
        ModeTemplate(
            state   = {Register.X: 0x02},
            apply   = lambda op, operands: [(0x0000, [op, 0xFF, 0x0F]), (0x1001, [operands['Memory']])],
            tag     = "xpage",
            eaddr   = 0x1001,
            xpage   = True,
        ),
    ],
    AddressModeId.AbsoluteY: [
        ModeTemplate(
            state   = {Register.Y: 0x01},
            apply   = lambda op, operands: [(0x0000, [op, 0x00, 0x10]), (0x1001, [operands['Memory']])],
            eaddr   = 0x1001,
        ),
        ModeTemplate(
            state   = {Register.Y:0x02},
            apply   = lambda op, operands: [(0x0000, [op, 0xFF, 0x0F]), (0x1001, [operands['Memory']])],
            tag     = "xpage",
            eaddr   = 0x1001,
            xpage   = True,
        ),
    ],
    AddressModeId.IndirectX: [
        ModeTemplate(
            state   = {Register.X: 0x01},
            apply   = lambda op, operands: [(0x0000, [op, 0x01, 0x80]), (0x0080, [operands['Memory']])],
            eaddr   = 0x0080,
        ),
        ModeTemplate(
            state   = {Register.X: 0x03},
            apply   = lambda op, operands: [(0x0000, [op, 0xFF, 0x80]), (0x0080, [operands['Memory']])],
            tag     = "overflow",
            eaddr   = 0x0080,
        ),
    ],
    AddressModeId.IndirectY: [
        ModeTemplate(
            state   = {Register.Y: 0x04},
            apply   = lambda op, operands: [(0x0000, [op, 0x02, 0x80, 0x10]), (0x1084, [operands['Memory']])],
            eaddr   = 0x1084,
        ),
        ModeTemplate(
            state   = {Register.Y: 0x80},
            apply   = lambda op, operands: [(0x0000, [op, 0x02, 0x80, 0x10]), (0x1100, [operands['Memory']])],
            tag     = "xpage",
            eaddr   = 0x1100,
            xpage   = True,
        ),
    ],
}

jump_templates: dict[AddressModeId, list[ModeTemplate]] = {
    AddressModeId.Absolute: [
        ModeTemplate(
            apply   = lambda op, operands: [
                        (0x0000, [op, operands['Memory'] & 0xFF, (operands['Memory'] >> 8) & 0xFF]),
                        (operands['Memory'], [0x00])
                      ],
        ),
    ],
    AddressModeId.Indirect: [
        ModeTemplate(
            apply   = lambda op, operands: [
                        (0x0000, [op, 0x01, 0x10]),
                        (0x1001, [operands['Memory'] & 0xFF, (operands['Memory'] >> 8) & 0xFF]),
                        (operands['Memory'], [0x00])
                      ],
            eaddr   = 0x1001,
        ),
    ],
    AddressModeId.Relative: [
        ModeTemplate(
            apply   = lambda op, operands: [
                        (0x0000, [op, operands['Memory'] & 0xFF]),
                        ((operands['Memory'] & 0xFF) + 3, [0x00])
                      ],
            eaddr   = 0x1001,
        ),
    ],
}

Operand: TypeAlias = Union[Register, Literal['Memory'], Literal['Flags'], Literal['Stack']]
Semantics = Callable[dict[Operand, int], dict[Operand, int]]

@dataclass
class Instruction:
    mnemonic:   str
    modes:      dict[AddressModeId, tuple[byte, int]] # mode -> (opcode, timing)
    testcases:  dict[Operand, [int]]
    semantics:  Semantics
    flagmask:   int = 0
    xpagestall: bool = False
    templates:  dict[AddressModeId, list[ModeTemplate]] = None

    def __post_init__(self):
        if self.templates is None:
            self.templates = default_templates

# Arithmetic flags side effects
def flag_z(v: int): return StatusFlags.Z if (v & 0xFF) == 0 else 0
def flag_n(v: int): return StatusFlags.N if (v & 0xFF) & 0x80 else 0
def flag_c(v: int): return StatusFlags.C if v > 0xFF else 0
def flag_v(v1: int, v2: int, carry: bool):
    # The computation here is boring on purpose,
    # to avoid reusing the same bit-twiddling optimization (and its bugs) as in the code we test.
    res = (v1 + v2 + carry) & 0xFF
    sr = -1 if (res & 0x80) else 1
    s1 = -1 if (v1 & 0x80) else 1
    s2 = -1 if (v2 & 0x80) else 1

    # 2's complement signed overflow rule: when operand signs are equal and the result sign differs
    return StatusFlags.V if (s1 == s2) and (sr != s1) else 0

def dec_u8(v: byte): return (v - 1) & 0xFF;
def inc_u8(v: byte): return (v + 1) & 0xFF;

def data_move_flags(v: int): return flag_z(v) | flag_n(v)

def branch_semantics(predicate):
    def semantics(tc):
        baseaddr = 0x0001
        destaddr = (tc['Memory'] if predicate(tc) else 0) + 3
        timing = 2 if not predicate(tc) else 3 if (destaddr & 0xFF00) == (baseaddr & 0xFF00) else 4

        return {
            Register.PC: destaddr,
            'Cycles':    timing,
        }

    return semantics

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
                        'Flags':    data_move_flags(tc['Memory']),
                      },
        testcases   = {'Memory': [0x42, 0xAA, 0x00], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
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
                        'Flags':    data_move_flags(tc['Memory']),
                      },
        testcases   = {'Memory': [0x42, 0xAA, 0x00], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
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
                        'Flags':    data_move_flags(tc['Memory']),
                      },
        testcases   = {'Memory': [0x42, 0xAA, 0x00], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
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
        testcases   = {Register.X: [0x42], 'Memory': [0x00]},
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
        testcases   = {Register.Y: [0x42], 'Memory': [0x00]},
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
        testcases   = {Register.A: [0x42], 'Memory': [0x00]},
    ),
    Instruction(
        mnemonic    = 'TAX',
        modes       = {
                        AddressModeId.Implied:      (0xAA, 2)
                      },
        semantics   = lambda tc: {
                        Register.X: tc[Register.A],
                        'Flags':    data_move_flags(tc[Register.A]),
                      },
        testcases   = {Register.A: [0x00, 0xAA, 0x42], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'TAY',
        modes       = {
                        AddressModeId.Implied:      (0xA8, 2)
                      },
        semantics   = lambda tc: {
                        Register.Y: tc[Register.A],
                        'Flags':    data_move_flags(tc[Register.A]),
                      },
        testcases   = {Register.A: [0x00, 0xAA, 0x42], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'TSX',
        modes       = {
                        AddressModeId.Implied:      (0xBA, 2)
                      },
        semantics   = lambda tc: {
                        Register.X: tc[Register.SP],
                        'Flags':    data_move_flags(tc[Register.SP]),
                      },
        testcases   = {Register.SP: [0x00, 0xAA, 0x42], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'TXA',
        modes       = {
                        AddressModeId.Implied:      (0x8A, 2)
                      },
        semantics   = lambda tc: {
                        Register.A: tc[Register.X],
                        'Flags':    data_move_flags(tc[Register.X]),
                      },
        testcases   = {Register.X: [0x00, 0xAA, 0x42], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'TXS',
        modes       = {
                        AddressModeId.Implied:      (0x9A, 2)
                      },
        semantics   = lambda tc: {
                        Register.SP: tc[Register.X],
                        'Flags':     data_move_flags(tc[Register.X]),
                      },
        testcases   = {Register.X: [0x00, 0xAA, 0x42], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'TYA',
        modes       = {
                        AddressModeId.Implied:      (0x98, 2)
                      },
        semantics   = lambda tc: {
                        Register.A: tc[Register.Y],
                        'Flags':    data_move_flags(tc[Register.Y]),
                      },
        testcases   = {Register.Y: [0x00, 0xAA, 0x42], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
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
                        Register.A:  tc['Stack'],
                        Register.SP: tc[Register.SP] + 1,
                        'Flags':     data_move_flags(tc['Stack']),
                      },
        testcases   = { 'Stack': [0x00, 0xAA, 0x42], Register.SP: [0xFC], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
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
                        'Flags':     tc['Stack'] & ~0x30,
                        Register.SP: tc[Register.SP] + 1,
                      },
        testcases   = { 'Stack': [0xAA], 'Flags':[0x55], Register.SP: [0xFC] },
        flagmask    = 0xFF & ~0x30, # PLP affects all flags except for bits 4 and 5
    ),
    Instruction(
        mnemonic    = 'DEC',
        modes       = {
                        AddressModeId.Zeropage:     (0xC6, 5),
                        AddressModeId.ZeropageX:    (0xD6, 6),
                        AddressModeId.Absolute:     (0xCE, 6),
                        AddressModeId.AbsoluteX:    (0xDE, 7),
                      },
        semantics   = lambda tc: {
                        'Memory': dec_u8(tc['Memory']),
                        'Flags':  data_move_flags(dec_u8(tc['Memory'])),
                      },
        testcases   = {'Memory': [0x01, 0xAA, 0x42, 0x00], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'INC',
        modes       = {
                        AddressModeId.Zeropage:     (0xE6, 5),
                        AddressModeId.ZeropageX:    (0xF6, 6),
                        AddressModeId.Absolute:     (0xEE, 6),
                        AddressModeId.AbsoluteX:    (0xFE, 7),
                      },
        semantics   = lambda tc: {
                        'Memory': inc_u8(tc['Memory']),
                        'Flags':  data_move_flags(inc_u8(tc['Memory'])),
                      },
        testcases   = {'Memory': [0xFF, 0xAA, 0x42, 0x00], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'DEX',
        modes       = {
                        AddressModeId.Implied:      (0xCA, 2),
                      },
        semantics   = lambda tc: {
                        Register.X: dec_u8(tc[Register.X]),
                        'Flags': data_move_flags(dec_u8(tc[Register.X])),
                      },
        testcases   = {Register.X: [0x00, 0xAA, 0x42, 0xFF], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'DEY',
        modes       = {
                        AddressModeId.Implied:      (0x88, 2),
                      },
        semantics   = lambda tc: {
                        Register.Y: dec_u8(tc[Register.Y]),
                        'Flags': data_move_flags(dec_u8(tc[Register.Y])),
                      },
        testcases   = {Register.Y: [0x00, 0xAA, 0x42, 0xFF], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'INX',
        modes       = {
                        AddressModeId.Implied:      (0xE8, 2),
                      },
        semantics   = lambda tc: {
                        Register.X: inc_u8(tc[Register.X]),
                        'Flags': data_move_flags(inc_u8(tc[Register.X])),
                      },
        testcases   = {Register.X: [0x00, 0xAA, 0x42, 0xFF], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'INY',
        modes       = {
                        AddressModeId.Implied:      (0xC8, 2),
                      },
        semantics   = lambda tc: {
                        Register.Y: inc_u8(tc[Register.Y]),
                        'Flags': data_move_flags(inc_u8(tc[Register.Y])),
                      },
        testcases   = {Register.Y: [0x00, 0xAA, 0x42, 0xFF], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
    ),
    Instruction(
        mnemonic    = 'ADC',
        modes       = {
                        AddressModeId.Immediate:    (0x69, 2),
                        AddressModeId.Zeropage:     (0x65, 3),
                        AddressModeId.ZeropageX:    (0x75, 4),
                        AddressModeId.Absolute:     (0x6D, 4),
                        AddressModeId.AbsoluteX:    (0x7D, 4),
                        AddressModeId.AbsoluteY:    (0x79, 4),
                        AddressModeId.IndirectX:    (0x61, 6),
                        AddressModeId.IndirectY:    (0x71, 5),
                      },
        semantics   = lambda tc: (
                        (lambda v: {
                          Register.A: v & 0xFF,
                          'Flags': data_move_flags(v) |
                                   flag_c(v) |
                                   flag_v(tc[Register.A], tc['Memory'], tc['Flags'] & StatusFlags.C),
                        })(tc[Register.A] + tc['Memory'] + ((tc['Flags'] & StatusFlags.C) != 0))
                      ),
        testcases   = {Register.A: [0x00, 0x42, 0xAA, 0xFF], 'Memory': [0x00, 0x01], 'Flags': [StatusFlags.C, 0x00]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C | StatusFlags.V,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'SBC',
        modes       = {
                        AddressModeId.Immediate:    (0xE9, 2),
                        AddressModeId.Zeropage:     (0xE5, 3),
                        AddressModeId.ZeropageX:    (0xF5, 4),
                        AddressModeId.Absolute:     (0xED, 4),
                        AddressModeId.AbsoluteX:    (0xFD, 4),
                        AddressModeId.AbsoluteY:    (0xF9, 4),
                        AddressModeId.IndirectX:    (0xE1, 6),
                        AddressModeId.IndirectY:    (0xF1, 5),
                      },
        semantics   = lambda tc: (
                        (lambda v: {
                          Register.A: v & 0xFF,
                          'Flags': data_move_flags(v) |
                                   flag_c(v) |
                                   flag_v(tc[Register.A], ~tc['Memory'] & 0xFF, tc['Flags'] & StatusFlags.C),
                        })(tc[Register.A] + (~tc['Memory'] & 0xFF) + ((tc['Flags'] & StatusFlags.C) != 0))
                      ),
        testcases   = {Register.A: [0x00, 0x42, 0xAA, 0xFF], 'Memory': [0xFF, 0xFE], 'Flags': [StatusFlags.C, 0x00]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C | StatusFlags.V,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'AND',
        modes       = {
                        AddressModeId.Immediate:    (0x29, 2),
                        AddressModeId.Zeropage:     (0x25, 3),
                        AddressModeId.ZeropageX:    (0x35, 4),
                        AddressModeId.Absolute:     (0x2D, 4),
                        AddressModeId.AbsoluteX:    (0x3D, 4),
                        AddressModeId.AbsoluteY:    (0x39, 4),
                        AddressModeId.IndirectX:    (0x21, 6),
                        AddressModeId.IndirectY:    (0x31, 5),
                      },
        semantics   = lambda tc: (
                        (lambda v: {Register.A: v, 'Flags': data_move_flags(v)})
                            (tc[Register.A] & tc['Memory'])
                      ),
        testcases   = {Register.A: [0x00, 0xFF, 0x10], 'Memory': [0x00, 0xFF, 0x01], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'EOR',
        modes       = {
                        AddressModeId.Immediate:    (0x49, 2),
                        AddressModeId.Zeropage:     (0x45, 3),
                        AddressModeId.ZeropageX:    (0x55, 4),
                        AddressModeId.Absolute:     (0x4D, 4),
                        AddressModeId.AbsoluteX:    (0x5D, 4),
                        AddressModeId.AbsoluteY:    (0x59, 4),
                        AddressModeId.IndirectX:    (0x41, 6),
                        AddressModeId.IndirectY:    (0x51, 5),
                      },
        semantics   = lambda tc: (
                        (lambda v: {Register.A: v, 'Flags': data_move_flags(v)})
                            (tc[Register.A] ^ tc['Memory'])
                      ),
        testcases   = {Register.A: [0x00, 0xFF, 0x10], 'Memory': [0x00, 0xFF, 0x01], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'ORA',
        modes       = {
                        AddressModeId.Immediate:    (0x09, 2),
                        AddressModeId.Zeropage:     (0x05, 3),
                        AddressModeId.ZeropageX:    (0x15, 4),
                        AddressModeId.Absolute:     (0x0D, 4),
                        AddressModeId.AbsoluteX:    (0x1D, 4),
                        AddressModeId.AbsoluteY:    (0x19, 4),
                        AddressModeId.IndirectX:    (0x01, 6),
                        AddressModeId.IndirectY:    (0x11, 5),
                      },
        semantics   = lambda tc: (
                        (lambda v: {Register.A: v, 'Flags': data_move_flags(v)})
                            (tc[Register.A] | tc['Memory'])
                      ),
        testcases   = {Register.A: [0x00, 0xFF, 0x10], 'Memory': [0x00, 0xFF, 0x01], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N]},
        flagmask    = StatusFlags.N | StatusFlags.Z,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'ASLA', # ASL/accumulator
        modes       = {
                        AddressModeId.Implied:      (0x0A, 2),
                      },
        semantics   = lambda tc: (
                        (lambda v: {Register.A: v, 'Flags': data_move_flags(v) | (StatusFlags.C if (tc[Register.A] & 0x80) else 0)})
                            ((tc[Register.A] << 1) & 0xFF)
                      ),
        testcases   = {Register.A: [0x80, 0xAA, 0x55]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
    ),
    Instruction(
        mnemonic    = 'ASLM', # ASL/memory
        modes       = {
                        AddressModeId.Zeropage:     (0x06, 5),
                        AddressModeId.ZeropageX:    (0x16, 6),
                        AddressModeId.Absolute:     (0x0E, 6),
                        AddressModeId.AbsoluteX:    (0x1E, 7),
                      },
        semantics   = lambda tc: (
                        (lambda v: {'Memory': v, 'Flags': data_move_flags(v) | (StatusFlags.C if (tc['Memory'] & 0x80) else 0)})
                            ((tc['Memory'] << 1) & 0xFF)
                      ),
        testcases   = {'Memory': [0x80, 0xAA, 0x55], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N | StatusFlags.C]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
    ),
    Instruction(
        mnemonic    = 'LSRA', # LSR/accumulator
        modes       = {
                        AddressModeId.Implied:      (0x4A, 2),
                      },
        semantics   = lambda tc: (
                        (lambda v: {Register.A: v, 'Flags': data_move_flags(v) | (StatusFlags.C if (tc[Register.A] & 0x01) else 0)})
                            ((tc[Register.A] >> 1) & 0xFF)
                      ),
        testcases   = {Register.A: [0x80, 0x01, 0x55], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N | StatusFlags.C]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
    ),
    Instruction(
        mnemonic    = 'LSRM', # LSR/memory
        modes       = {
                        AddressModeId.Zeropage:     (0x46, 5),
                        AddressModeId.ZeropageX:    (0x56, 6),
                        AddressModeId.Absolute:     (0x4E, 6),
                        AddressModeId.AbsoluteX:    (0x5E, 7),
                      },
        semantics   = lambda tc: (
                        (lambda v: {'Memory': v, 'Flags': data_move_flags(v) | (StatusFlags.C if (tc['Memory'] & 0x01) else 0)})
                            ((tc['Memory'] >> 1) & 0xFF)
                      ),
        testcases   = {'Memory': [0x80, 0x01, 0x55], 'Flags': [0x00, StatusFlags.Z | StatusFlags.N | StatusFlags.C]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
    ),
    Instruction(
        mnemonic    = 'ROLA', # ROL/accumulator
        modes       = {
                        AddressModeId.Implied:      (0x2A, 2),
                      },
        semantics   = lambda tc: (
                        (lambda v: {Register.A: v, 'Flags': data_move_flags(v) | (StatusFlags.C if (tc[Register.A] & 0x80) else 0)
                        })(((tc[Register.A] << 1) & 0xFF) | (0x01 if (tc['Flags'] & StatusFlags.C) else 0))
                      ),
        testcases   = {Register.A: [0x80, 0x01, 0x55], 'Flags': [StatusFlags.C, 0x00]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
    ),
    Instruction(
        mnemonic    = 'ROLM', # LSR/memory
        modes       = {
                        AddressModeId.Zeropage:     (0x26, 5),
                        AddressModeId.ZeropageX:    (0x36, 6),
                        AddressModeId.Absolute:     (0x2E, 6),
                        AddressModeId.AbsoluteX:    (0x3E, 7),
                      },
        semantics   = lambda tc: (
                        (lambda v: {'Memory': v, 'Flags': data_move_flags(v) | (StatusFlags.C if (tc['Memory'] & 0x80) else 0)
                        })(((tc['Memory'] << 1) & 0xFF) | (0x01 if (tc['Flags'] & StatusFlags.C) else 0))
                      ),
        testcases   = {'Memory': [0x80, 0x01, 0x55], 'Flags': [StatusFlags.C, 0x00]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
    ),
    Instruction(
        mnemonic    = 'RORA', # ROR/accumulator
        modes       = {
                        AddressModeId.Implied:      (0x6A, 2),
                      },
        semantics   = lambda tc: (
                        (lambda v: {Register.A: v, 'Flags': data_move_flags(v) | (StatusFlags.C if (tc[Register.A] & 0x01) else 0)
                        })(((tc[Register.A] >> 1) & 0xFF) | (0x80 if (tc['Flags'] & StatusFlags.C) else 0))
                      ),
        testcases   = {Register.A: [0x80, 0x01, 0x55], 'Flags': [StatusFlags.C, 0x00]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
    ),
    Instruction(
        mnemonic    = 'RORM', # LSR/memory
        modes       = {
                        AddressModeId.Zeropage:     (0x66, 5),
                        AddressModeId.ZeropageX:    (0x76, 6),
                        AddressModeId.Absolute:     (0x6E, 6),
                        AddressModeId.AbsoluteX:    (0x7E, 7),
                      },
        semantics   = lambda tc: (
                        (lambda v: {'Memory': v, 'Flags': data_move_flags(v) | (StatusFlags.C if (tc['Memory'] & 0x01) else 0)
                        })(((tc['Memory'] >> 1) & 0xFF) | (0x80 if (tc['Flags'] & StatusFlags.C) else 0))
                      ),
        testcases   = {'Memory': [0x80, 0x01, 0x55], 'Flags': [StatusFlags.C, 0x00]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
    ),
    Instruction(
        mnemonic    = 'CLC',
        modes       = {AddressModeId.Implied: (0x18, 2)},
        semantics   = lambda tc: ({'Flags': 0}),
        flagmask    = StatusFlags.C,
        testcases   = {'Flags': [StatusFlags.C, 0]},
    ),
    Instruction(
        mnemonic    = 'CLI',
        modes       = {AddressModeId.Implied: (0x58, 2)},
        semantics   = lambda tc: ({'Flags': 0}),
        flagmask    = StatusFlags.I,
        testcases   = {'Flags': [StatusFlags.I, 0]},
    ),
    Instruction(
        mnemonic    = 'CLV',
        modes       = {AddressModeId.Implied: (0xB8, 2)},
        semantics   = lambda tc: ({'Flags': 0}),
        flagmask    = StatusFlags.V,
        testcases   = {'Flags': [StatusFlags.V, 0]},
    ),
    Instruction(
        mnemonic    = 'SEC',
        modes       = {AddressModeId.Implied: (0x38, 2)},
        semantics   = lambda tc: ({'Flags': StatusFlags.C}),
        flagmask    = StatusFlags.C,
        testcases   = {'Flags': [StatusFlags.C, 0]},
    ),
    Instruction(
        mnemonic    = 'SEI',
        modes       = {AddressModeId.Implied: (0x78, 2)},
        semantics   = lambda tc: ({'Flags': StatusFlags.I}),
        flagmask    = StatusFlags.I,
        testcases   = {'Flags': [StatusFlags.I, 0]},
    ),
    Instruction(
        mnemonic    = 'BIT',
        modes       = {
                        AddressModeId.Zeropage: (0x24, 3),
                        AddressModeId.Absolute: (0x2C, 4),
                      },
        semantics   = lambda tc: ({
                        'Flags': (StatusFlags.Z if tc[Register.A] == tc['Memory'] else 0) | (tc['Memory'] & (StatusFlags.N | StatusFlags.V))
                      }),
        flagmask    = StatusFlags.N | StatusFlags.V | StatusFlags.Z,
        testcases   = {Register.A:[0xAA, 0x55], 'Memory':[0x55, 0xAA], 'Flags':[0x00, StatusFlags.N | StatusFlags.V | StatusFlags.Z]},
    ),
    Instruction(
        mnemonic    = 'CMP',
        modes       = {
                        AddressModeId.Immediate: (0xC9, 2),
                        AddressModeId.Zeropage:  (0xC5, 3),
                        AddressModeId.ZeropageX: (0xD5, 4),
                        AddressModeId.Absolute:  (0xCD, 4),
                        AddressModeId.AbsoluteX: (0xDD, 4),
                        AddressModeId.AbsoluteY: (0xD9, 4),
                        AddressModeId.IndirectX: (0xC1, 6),
                        AddressModeId.IndirectY: (0xD1, 5),
                      },
        semantics   = lambda tc: ({
                        'Flags': (StatusFlags.C if tc[Register.A] >= tc['Memory'] else 0) |
                                 (StatusFlags.Z if tc[Register.A] == tc['Memory'] else 0) |
                                 (StatusFlags.N if ((tc[Register.A] - tc['Memory']) & 0x80) else 0)
                      }),
        testcases   = {Register.A:[0xAA, 0x55], 'Memory':[0x55, 0xAA], 'Flags':[0x00, StatusFlags.N | StatusFlags.V | StatusFlags.Z]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'CPX',
        modes       = {
                        AddressModeId.Immediate: (0xE0, 2),
                        AddressModeId.Zeropage:  (0xE4, 3),
                        AddressModeId.Absolute:  (0xEC, 4),
                      },
        semantics   = lambda tc: ({
                        'Flags': (StatusFlags.C if tc[Register.X] >= tc['Memory'] else 0) |
                                 (StatusFlags.Z if tc[Register.X] == tc['Memory'] else 0) |
                                 (StatusFlags.N if ((tc[Register.X] - tc['Memory']) & 0x80) else 0)
                      }),
        testcases   = {Register.X:[0xAA, 0x55], 'Memory':[0x55, 0xAA], 'Flags':[0x00, StatusFlags.N | StatusFlags.V | StatusFlags.Z]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'CPY',
        modes       = {
                        AddressModeId.Immediate: (0xC0, 2),
                        AddressModeId.Zeropage:  (0xC4, 3),
                        AddressModeId.Absolute:  (0xCC, 4),
                      },
        semantics   = lambda tc: ({
                        'Flags': (StatusFlags.C if tc[Register.Y] >= tc['Memory'] else 0) |
                                 (StatusFlags.Z if tc[Register.Y] == tc['Memory'] else 0) |
                                 (StatusFlags.N if ((tc[Register.Y] - tc['Memory']) & 0x80) else 0)
                      }),
        testcases   = {Register.Y:[0xAA, 0x55], 'Memory':[0x55, 0xAA], 'Flags':[0x00, StatusFlags.N | StatusFlags.V | StatusFlags.Z]},
        flagmask    = StatusFlags.N | StatusFlags.Z | StatusFlags.C,
        xpagestall  = True,
    ),
    Instruction(
        mnemonic    = 'JMP',
        modes       = {
                        AddressModeId.Absolute: (0x4C, 3),
                        AddressModeId.Indirect: (0x6C, 5),
                      },
        semantics   = lambda tc: ({
                        Register.PC: tc['Memory'] + 1
                      }),
        testcases   = {'Memory':[0x2001]},
        templates   = jump_templates,
    ),
    Instruction(
        mnemonic    = 'BCC',
        modes       = {AddressModeId.Relative: (0x90, 2)},
        semantics   = branch_semantics(lambda tc: not (tc['Flags'] & StatusFlags.C)),
        testcases   = {'Memory':[0x08, 0xFF], 'Flags':[0x00, StatusFlags.C]},
        templates   = jump_templates,
    ),
    Instruction(
        mnemonic    = 'BCS',
        modes       = {AddressModeId.Relative: (0xB0, 2)},
        semantics   = branch_semantics(lambda tc: (tc['Flags'] & StatusFlags.C)),
        testcases   = {'Memory':[0x08, 0xFF], 'Flags':[0x00, StatusFlags.C]},
        templates   = jump_templates,
    ),
    Instruction(
        mnemonic    = 'BNE',
        modes       = {AddressModeId.Relative: (0xD0, 2)},
        semantics   = branch_semantics(lambda tc: not (tc['Flags'] & StatusFlags.Z)),
        testcases   = {'Memory':[0x08, 0xFF], 'Flags':[0x00, StatusFlags.Z]},
        templates   = jump_templates,
    ),
    Instruction(
        mnemonic    = 'BEQ',
        modes       = {AddressModeId.Relative: (0xF0, 2)},
        semantics   = branch_semantics(lambda tc: (tc['Flags'] & StatusFlags.Z)),
        testcases   = {'Memory':[0x08, 0xFF], 'Flags':[0x00, StatusFlags.Z]},
        templates   = jump_templates,
    ),
    Instruction(
        mnemonic    = 'BPL',
        modes       = {AddressModeId.Relative: (0x10, 2)},
        semantics   = branch_semantics(lambda tc: not (tc['Flags'] & StatusFlags.N)),
        testcases   = {'Memory':[0x08, 0xFF], 'Flags':[0x00, StatusFlags.N]},
        templates   = jump_templates,
    ),
    Instruction(
        mnemonic    = 'BMI',
        modes       = {AddressModeId.Relative: (0x30, 2)},
        semantics   = branch_semantics(lambda tc: (tc['Flags'] & StatusFlags.N)),
        testcases   = {'Memory':[0x08, 0xFF], 'Flags':[0x00, StatusFlags.N]},
        templates   = jump_templates,
    ),
    Instruction(
        mnemonic    = 'BVC',
        modes       = {AddressModeId.Relative: (0x50, 2)},
        semantics   = branch_semantics(lambda tc: not (tc['Flags'] & StatusFlags.V)),
        testcases   = {'Memory':[0x08, 0xFF], 'Flags':[0x00, StatusFlags.V]},
        templates   = jump_templates,
    ),
    Instruction(
        mnemonic    = 'BVS',
        modes       = {AddressModeId.Relative: (0x70, 2)},
        semantics   = branch_semantics(lambda tc: (tc['Flags'] & StatusFlags.V)),
        testcases   = {'Memory':[0x08, 0xFF], 'Flags':[0x00, StatusFlags.V]},
        templates   = jump_templates,
    ),
]

print(f"/* This file is auto-generated from {Path(__file__).name} */\n");

def expand_operand_values(spec: dict[Operand, list[int]]):
    keys = spec.keys()
    for p in product(*spec.values()):
        yield dict(zip(keys, p))

def gen_instruction_tests(instr: Instruction) -> None:
    for testcase in expand_operand_values(instr.testcases):
        for mode, (opcode, timing) in instr.modes.items():
            for mode_template in instr.templates[mode]:
                template_tag = f"_{mode_template.tag}" if mode_template.tag else ""
                testcase_tag = "_".join(f"{v:02x}" for v in testcase.values())
                test_name = f"test_{instr.mnemonic}_{mode.value}{template_tag}_{testcase_tag}".lower()

                expected = instr.semantics(testcase)
                segments = mode_template.apply(opcode, testcase)

                print(f"ep_test({test_name})")
                print( "{")
                print( "    const struct test_ram_segment segments[] = {")
                for base, data in segments:
                    hex_bytes = ", ".join(f"0x{b:02x}" for b in data)
                    print(f"        MAKE_TEST_SEGMENT_VEC(0x{base:04x}, {{{hex_bytes}}}),")
                print( "    };")
                print()

                print( "    struct mos6502_cpu cpu;")
                print(f"    init_test_cpu(&cpu, segments, {len(segments)});");
                print()

                # Setup the src operands and template state
                for key, value in testcase.items():
                    if isinstance(key, Register):
                        print(f"    cpu.{key.value} = 0x{value:02x};");
                    elif key == 'Stack':
                        sp = testcase[Register.SP] + 1
                        print(f"    mos6502_store_word(&cpu, 0x0100 | 0x{sp:02x}, 0x{value:02x});")
                    elif key == 'Flags':
                        print(f"    cpu.P = (cpu.P & ~0x{instr.flagmask}) | 0x{value:02x};")
                    elif key != 'Memory':
                        raise ValueError("Invalid operand type")

                if mode_template.state:
                    for reg, val in mode_template.state.items():
                        print(f"    cpu.{reg.value} = 0x{val:02x};");

                print(f"    mos_word_t orig_flags = cpu.P;");
                print(f"    uint64_t cycles = run_test_cpu(&cpu);");
                print()

                if expected.get('Cycles'):
                    effective_cycles = expected['Cycles'];
                else:
                    effective_cycles = timing + (1 if instr.xpagestall and mode_template.xpage else 0)

                print(f"    ep_verify_equal(cycles, {effective_cycles});")
                print(f"    ep_verify_equal(cpu.total_retired, 1);");

                # Validate the instr expected output
                for key, value in expected.items():
                    if isinstance(key, Register):
                        print(f"    ep_verify_equal(cpu.{key.value}, 0x{value:02x});")
                    elif key == 'Memory':
                        print(f"    ep_verify_equal(mos6502_load_word(&cpu, 0x{mode_template.eaddr:04x}), 0x{value:02x});")
                    elif key == 'Flags':
                        print(f"    ep_verify_equal(cpu.P & 0x{instr.flagmask:02x}, 0x{value:02x});");
                    elif key == 'Stack':
                        print(f"    ep_verify_equal(mos6502_load_word(&cpu, 0x0100 | (cpu.SP + 1)), 0x{value:02x});")
                    elif key != 'Cycles':
                        raise ValueError("Invalid output operand type")

                # Instruction shouldn't've modified the flags that are not in its affected flags mask
                print(f"    ep_verify_equal(cpu.P & ~0x{instr.flagmask:02x}, orig_flags & ~0x{instr.flagmask:02x});")
                print()
                print( "    free_test_cpu(&cpu);");
                print( "}\n")

for instr in instructions:
    gen_instruction_tests(instr)
