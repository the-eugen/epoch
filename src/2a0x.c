/*
 * Ricoh 2A03/2A07 CPU emulation.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "defs.h"
#include "2a0x.h"

enum mos6502_tstate
{
    MOS_TSTATE_FETCH,
    MOS_TSTATE_ADDRESS_LATCH,
    MOS_TSTATE_UOP,
};

struct mos6502_bus_trace
{
    uint64_t cycle;
    mos_paddr_t addr;
    union {
        uint8_t as_u8;
        struct {
            uint8_t rw:1;
            uint8_t discard:1;
            uint8_t _unused:6;
        };
    } flags;
};

struct mos6502_cpu
{
    union {
        mos_paddr_t PC;
        struct {
            mos_word_t PCL;
            mos_word_t PCH;
        };
    };
    mos_word_t A;
    mos_word_t X;
    mos_word_t Y;
    mos_word_t P;
    #define SR_N (1u << 7)
    #define SR_V (1u << 6)
    #define SR_U (1u << 5)
    #define SR_B (1u << 4)
    #define SR_D (1u << 3)
    #define SR_I (1u << 2)
    #define SR_Z (1u << 1)
    #define SR_C (1u << 0)
    mos_word_t SP;

    /* Internal register for effective address storage */
    union {
        mos_paddr_t eaddr;
        struct {
            mos_word_t eaddrl;
            mos_word_t eaddrh;
        };
    };

    /* Internal register for indirect address for ops that use it */
    union {
        mos_paddr_t iaddr;
        struct {
            mos_word_t iaddrl;
            mos_word_t iaddrh;
        };
    };

    /* Internal data bus register */
    mos_word_t db;

    bool halted;
    enum mos6502_tstate tstate;

    struct mos6502_instr instr;
    uint8_t instr_cycle;

    uint64_t cycle;
    uint64_t total_retired;

#ifdef EP_CONFIG_TEST
    /* Bus trace records last instruction externally-visible effects */
    #define EP_TEST_BUS_TRACE_SIZE 16
    struct mos6502_bus_trace bus_trace[EP_TEST_BUS_TRACE_SIZE];
    uint8_t bus_trace_head;
#endif
};

#define _MOS_OP_CTRLBITS(_opc_, _mnemonic_, _mode_, _ncycles_, _length_, _ctrlbits_) \
    [(_opc_)] = (struct mos6502_instr) { \
        .mnemonic = #_mnemonic_, \
        .uop = MOS_UOP_ ##_mnemonic_, \
        .mode = (_mode_), \
        .ncycles = (_ncycles_), \
        .length = (_length_), \
        .ctrlbits = (_ctrlbits_), \
    }

#define _MOS_OP(_opc_, _mnemonic_, _mode_, _ncycles_, _length_) \
    _MOS_OP_CTRLBITS(_opc_, _mnemonic_, _mode_, _ncycles_, _length_, 0)

#define _MOS_APPLY_N(_1, _2, _3, _4, _5, _6, _apply, ...) \
    _apply

#define MOS_OP(...) \
    _X(_MOS_APPLY_N(__VA_ARGS__, _MOS_OP_CTRLBITS, _MOS_OP)(__VA_ARGS__))

static const struct mos6502_instr mos_opcodes[] =
{
    MOS_OP(0xea, NOP, MOS_AM_IMP,  2, 1),

    MOS_OP(0x02, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0x12, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0x22, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0x32, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0x42, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0x52, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0x62, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0x72, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0x92, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0xb2, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0xd2, HLT, MOS_AM_IMP,  1, 1),
    MOS_OP(0xf2, HLT, MOS_AM_IMP,  1, 1),

    MOS_OP(0xa9, LDA, MOS_AM_IMM,  2, 2),
    MOS_OP(0xa5, LDA, MOS_AM_Z,    3, 2),
    MOS_OP(0xb5, LDA, MOS_AM_ZX,   4, 2),
    MOS_OP(0xad, LDA, MOS_AM_ABS,  4, 3),
    MOS_OP(0xbd, LDA, MOS_AM_ABSX, 4, 3),
    MOS_OP(0xb9, LDA, MOS_AM_ABSY, 4, 3),
    MOS_OP(0xa1, LDA, MOS_AM_INDX, 6, 2),
    MOS_OP(0xb1, LDA, MOS_AM_INDY, 5, 2),

    MOS_OP(0xa2, LDX, MOS_AM_IMM,  2, 2),
    MOS_OP(0xa6, LDX, MOS_AM_Z,    3, 2),
    MOS_OP(0xb6, LDX, MOS_AM_ZY,   4, 2),
    MOS_OP(0xae, LDX, MOS_AM_ABS,  4, 3),
    MOS_OP(0xbe, LDX, MOS_AM_ABSY, 4, 3),

    MOS_OP(0xa0, LDY, MOS_AM_IMM,  2, 2),
    MOS_OP(0xa4, LDY, MOS_AM_Z,    3, 2),
    MOS_OP(0xb4, LDY, MOS_AM_ZX,   4, 2),
    MOS_OP(0xac, LDY, MOS_AM_ABS,  4, 3),
    MOS_OP(0xbc, LDY, MOS_AM_ABSX, 4, 3),

    MOS_OP(0x85, STA, MOS_AM_Z,    3, 2),
    MOS_OP(0x95, STA, MOS_AM_ZX,   4, 2),
    MOS_OP(0x8D, STA, MOS_AM_ABS,  4, 3),
    MOS_OP(0x9D, STA, MOS_AM_ABSX, 5, 3, MOS_CTRL_RW),
    MOS_OP(0x99, STA, MOS_AM_ABSY, 5, 3, MOS_CTRL_RW),
    MOS_OP(0x81, STA, MOS_AM_INDX, 6, 2),
    MOS_OP(0x91, STA, MOS_AM_INDY, 6, 2, MOS_CTRL_RW),

    MOS_OP(0x86, STX, MOS_AM_Z,    3, 2),
    MOS_OP(0x96, STX, MOS_AM_ZY,   4, 2),
    MOS_OP(0x8E, STX, MOS_AM_ABS,  4, 3),

    MOS_OP(0x84, STY, MOS_AM_Z,    3, 2),
    MOS_OP(0x94, STY, MOS_AM_ZX,   4, 2),
    MOS_OP(0x8C, STY, MOS_AM_ABS,  4, 3),

    MOS_OP(0xAA, TAX, MOS_AM_IMP,  2, 1),
    MOS_OP(0xA8, TAY, MOS_AM_IMP,  2, 1),
    MOS_OP(0xBA, TSX, MOS_AM_IMP,  2, 1),
    MOS_OP(0x8A, TXA, MOS_AM_IMP,  2, 1),
    MOS_OP(0x9A, TXS, MOS_AM_IMP,  2, 1),
    MOS_OP(0x98, TYA, MOS_AM_IMP,  2, 1),

    MOS_OP(0x48, PHA, MOS_AM_IMP,  3, 1),
    MOS_OP(0x68, PLA, MOS_AM_IMP,  4, 1),
    MOS_OP(0x08, PHP, MOS_AM_IMP,  3, 1),
    MOS_OP(0x28, PLP, MOS_AM_IMP,  4, 1),

    MOS_OP(0xC6, DEC, MOS_AM_Z,    5, 2),
    MOS_OP(0xD6, DEC, MOS_AM_ZX,   6, 2),
    MOS_OP(0xCE, DEC, MOS_AM_ABS,  6, 3),
    MOS_OP(0xDE, DEC, MOS_AM_ABSX, 7, 3, MOS_CTRL_RW),
    MOS_OP(0xCA, DEX, MOS_AM_IMP,  2, 1),
    MOS_OP(0x88, DEY, MOS_AM_IMP,  2, 1),

    MOS_OP(0xE6, INC, MOS_AM_Z,    5, 2),
    MOS_OP(0xF6, INC, MOS_AM_ZX,   6, 2),
    MOS_OP(0xEE, INC, MOS_AM_ABS,  6, 3),
    MOS_OP(0xFE, INC, MOS_AM_ABSX, 7, 3, MOS_CTRL_RW),
    MOS_OP(0xE8, INX, MOS_AM_IMP,  2, 1),
    MOS_OP(0xC8, INY, MOS_AM_IMP,  2, 1),

    MOS_OP(0x69, ADC, MOS_AM_IMM,  2, 2),
    MOS_OP(0x65, ADC, MOS_AM_Z,    3, 2),
    MOS_OP(0x75, ADC, MOS_AM_ZX,   4, 2),
    MOS_OP(0x6D, ADC, MOS_AM_ABS,  4, 3),
    MOS_OP(0x7D, ADC, MOS_AM_ABSX, 4, 3),
    MOS_OP(0x79, ADC, MOS_AM_ABSY, 4, 3),
    MOS_OP(0x61, ADC, MOS_AM_INDX, 6, 2),
    MOS_OP(0x71, ADC, MOS_AM_INDY, 5, 2),

    MOS_OP(0xE9, SBC, MOS_AM_IMM,  2, 2),
    MOS_OP(0xE5, SBC, MOS_AM_Z,    3, 2),
    MOS_OP(0xF5, SBC, MOS_AM_ZX,   4, 2),
    MOS_OP(0xED, SBC, MOS_AM_ABS,  4, 3),
    MOS_OP(0xFD, SBC, MOS_AM_ABSX, 4, 3),
    MOS_OP(0xF9, SBC, MOS_AM_ABSY, 4, 3),
    MOS_OP(0xE1, SBC, MOS_AM_INDX, 6, 2),
    MOS_OP(0xF1, SBC, MOS_AM_INDY, 5, 2),

    MOS_OP(0x29, AND, MOS_AM_IMM,  2, 2),
    MOS_OP(0x25, AND, MOS_AM_Z,    3, 2),
    MOS_OP(0x35, AND, MOS_AM_ZX,   4, 2),
    MOS_OP(0x2D, AND, MOS_AM_ABS,  4, 3),
    MOS_OP(0x3D, AND, MOS_AM_ABSX, 4, 3),
    MOS_OP(0x39, AND, MOS_AM_ABSY, 4, 3),
    MOS_OP(0x21, AND, MOS_AM_INDX, 6, 2),
    MOS_OP(0x31, AND, MOS_AM_INDY, 5, 2),

    MOS_OP(0x49, EOR, MOS_AM_IMM,  2, 2),
    MOS_OP(0x45, EOR, MOS_AM_Z,    3, 2),
    MOS_OP(0x55, EOR, MOS_AM_ZX,   4, 2),
    MOS_OP(0x4D, EOR, MOS_AM_ABS,  4, 3),
    MOS_OP(0x5D, EOR, MOS_AM_ABSX, 4, 3),
    MOS_OP(0x59, EOR, MOS_AM_ABSY, 4, 3),
    MOS_OP(0x41, EOR, MOS_AM_INDX, 6, 2),
    MOS_OP(0x51, EOR, MOS_AM_INDY, 5, 2),

    MOS_OP(0x09, ORA, MOS_AM_IMM,  2, 2),
    MOS_OP(0x05, ORA, MOS_AM_Z,    3, 2),
    MOS_OP(0x15, ORA, MOS_AM_ZX,   4, 2),
    MOS_OP(0x0D, ORA, MOS_AM_ABS,  4, 3),
    MOS_OP(0x1D, ORA, MOS_AM_ABSX, 4, 3),
    MOS_OP(0x19, ORA, MOS_AM_ABSY, 4, 3),
    MOS_OP(0x01, ORA, MOS_AM_INDX, 6, 2),
    MOS_OP(0x11, ORA, MOS_AM_INDY, 5, 2),

    MOS_OP(0x0A, ASL, MOS_AM_IMP,  2, 1),
    MOS_OP(0x06, ASL, MOS_AM_Z,    5, 2),
    MOS_OP(0x16, ASL, MOS_AM_ZX,   6, 2),
    MOS_OP(0x0E, ASL, MOS_AM_ABS,  6, 3),
    MOS_OP(0x1E, ASL, MOS_AM_ABSX, 7, 3, MOS_CTRL_RW),

    MOS_OP(0x4A, LSR, MOS_AM_IMP,  2, 1),
    MOS_OP(0x46, LSR, MOS_AM_Z,    5, 2),
    MOS_OP(0x56, LSR, MOS_AM_ZX,   6, 2),
    MOS_OP(0x4E, LSR, MOS_AM_ABS,  6, 3),
    MOS_OP(0x5E, LSR, MOS_AM_ABSX, 7, 3, MOS_CTRL_RW),

    MOS_OP(0x2A, ROL, MOS_AM_IMP,  2, 1),
    MOS_OP(0x26, ROL, MOS_AM_Z,    5, 2),
    MOS_OP(0x36, ROL, MOS_AM_ZX,   6, 2),
    MOS_OP(0x2E, ROL, MOS_AM_ABS,  6, 3),
    MOS_OP(0x3E, ROL, MOS_AM_ABSX, 7, 3, MOS_CTRL_RW),

    MOS_OP(0x6A, ROR, MOS_AM_IMP,  2, 1),
    MOS_OP(0x66, ROR, MOS_AM_Z,    5, 2),
    MOS_OP(0x76, ROR, MOS_AM_ZX,   6, 2),
    MOS_OP(0x6E, ROR, MOS_AM_ABS,  6, 3),
    MOS_OP(0x7E, ROR, MOS_AM_ABSX, 7, 3, MOS_CTRL_RW),

    MOS_OP(0x18, CLC, MOS_AM_IMP,  2, 1),
    MOS_OP(0xD8, CLD, MOS_AM_IMP,  2, 1),
    MOS_OP(0x58, CLI, MOS_AM_IMP,  2, 1),
    MOS_OP(0xB8, CLV, MOS_AM_IMP,  2, 1),
    MOS_OP(0x38, SEC, MOS_AM_IMP,  2, 1),
    MOS_OP(0xF8, SED, MOS_AM_IMP,  2, 1),
    MOS_OP(0x78, SEI, MOS_AM_IMP,  2, 1),

    MOS_OP(0x24, BIT, MOS_AM_Z,    3, 2),
    MOS_OP(0x2C, BIT, MOS_AM_ABS,  4, 3),

    MOS_OP(0xC9, CMP, MOS_AM_IMM,  2, 2),
    MOS_OP(0xC5, CMP, MOS_AM_Z,    3, 2),
    MOS_OP(0xD5, CMP, MOS_AM_ZX,   4, 2),
    MOS_OP(0xCD, CMP, MOS_AM_ABS,  4, 3),
    MOS_OP(0xDD, CMP, MOS_AM_ABSX, 4, 3),
    MOS_OP(0xD9, CMP, MOS_AM_ABSY, 4, 3),
    MOS_OP(0xC1, CMP, MOS_AM_INDX, 6, 2),
    MOS_OP(0xD1, CMP, MOS_AM_INDY, 5, 2),

    MOS_OP(0xE0, CPX, MOS_AM_IMM,  2, 2),
    MOS_OP(0xE4, CPX, MOS_AM_Z,    3, 2),
    MOS_OP(0xEC, CPX, MOS_AM_ABS,  4, 3),

    MOS_OP(0xC0, CPY, MOS_AM_IMM,  2, 2),
    MOS_OP(0xC4, CPY, MOS_AM_Z,    3, 2),
    MOS_OP(0xCC, CPY, MOS_AM_ABS,  4, 3),

    MOS_OP(0x4C, JMP, MOS_AM_ABS,  3, 3, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0x6C, JMP, MOS_AM_IND,  5, 3, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0x20, JSR, MOS_AM_IMP,  6, 3, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0x60, RTS, MOS_AM_IMP,  6, 1, MOS_CTRL_PC_ADDRESS_LATCH),

    MOS_OP(0x90, BCC, MOS_AM_IMP,  2, 2, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0xB0, BCS, MOS_AM_IMP,  2, 2, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0xF0, BEQ, MOS_AM_IMP,  2, 2, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0x30, BMI, MOS_AM_IMP,  2, 2, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0xD0, BNE, MOS_AM_IMP,  2, 2, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0x10, BPL, MOS_AM_IMP,  2, 2, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0x50, BVC, MOS_AM_IMP,  2, 2, MOS_CTRL_PC_ADDRESS_LATCH),
    MOS_OP(0x70, BVS, MOS_AM_IMP,  2, 2, MOS_CTRL_PC_ADDRESS_LATCH),
};

#ifdef EP_CONFIG_TEST

static void reset_bus_trace(struct mos6502_cpu* cpu)
{
    memset(cpu->bus_trace, 0, sizeof(cpu->bus_trace));
    cpu->bus_trace_head = 0;
}

static void record_bus_trace(struct mos6502_cpu* cpu, mos_paddr_t addr, bool rw, bool discard)
{
    // Writes can't be discarded
    ep_assert(!rw || !discard);

    struct mos6502_bus_trace* trace = &cpu->bus_trace[cpu->bus_trace_head];
    cpu->bus_trace_head = (cpu->bus_trace_head + 1) % EP_TEST_BUS_TRACE_SIZE;

    trace->cycle = cpu->cycle;
    trace->addr = addr;
    trace->flags.rw = !!rw;
    trace->flags.discard = !!discard;

    ep_trace("trace entry %u: cycle %lu, addr 0x%04hx, flags 0x%02hhx", cpu->bus_trace_head - 1, trace->cycle, trace->addr, trace->flags.as_u8);
}

#else

static inline void reset_bus_trace(struct mos6502_cpu* cpu)
{
    (void) cpu;
}

static inline void record_bus_trace(struct mos6502_cpu* cpu, mos_paddr_t addr, bool rw, bool discard)
{
    (void) cpu;
    (void) addr;
    (void) rw;
    (void) discard;
}

#endif

static mos_word_t fetch_word(struct mos6502_cpu* cpu, mos_paddr_t pa)
{
    /* TODO: Handle unmapped addresses */
    mos_word_t* vaddr = mos6502_decode_paddr(cpu, pa);
    ep_verify(vaddr);

    return *vaddr;
}

static void store_word(struct mos6502_cpu* cpu, mos_paddr_t pa, mos_word_t val)
{
    /* TODO: Handle unmapped addresses */
    mos_word_t* vaddr = mos6502_decode_paddr(cpu, pa);
    ep_verify(vaddr);

    *vaddr = val;
}

static void push_word(struct mos6502_cpu* cpu, mos_word_t val)
{
    ep_assert(cpu);

    /* Stack wraps around inside the segment */
    store_word(cpu, 0x0100 | cpu->SP, val);
    cpu->SP--;
}

static mos_word_t pop_word(struct mos6502_cpu* cpu)
{
    ep_assert(cpu);

    /* Stack wraps around inside the segment */
    cpu->SP++;
    return fetch_word(cpu, 0x0100 | cpu->SP);
}

static inline mos_word_t cpu_fetch(struct mos6502_cpu* cpu, mos_paddr_t pa)
{
    record_bus_trace(cpu, pa, false, false);
    return fetch_word(cpu, pa);
}

static inline void cpu_fetch_discard(struct mos6502_cpu* cpu, mos_paddr_t pa)
{
    record_bus_trace(cpu, pa, false, true);
    (void) fetch_word(cpu, pa);
}

static void cpu_store(struct mos6502_cpu* cpu, mos_paddr_t pa, mos_word_t val)
{
    record_bus_trace(cpu, pa, true, false);
    store_word(cpu, pa, val);
}

static inline mos_word_t cpu_pop(struct mos6502_cpu* cpu)
{
    record_bus_trace(cpu, (0x0100 | cpu->SP) + 1, false, false);
    return pop_word(cpu);
}

static inline void cpu_push(struct mos6502_cpu* cpu, mos_word_t val)
{
    record_bus_trace(cpu, 0x0100 | cpu->SP, true, false);
    push_word(cpu, val);
}

static void fetch_next_instr(struct mos6502_cpu* cpu)
{
    /* TODO: unimplemented instruction exception? */
    mos_word_t opcode = cpu_fetch(cpu, cpu->PC++);
    cpu->instr = mos_opcodes[opcode];
    cpu->instr_cycle = 0;
    ep_trace("[%04lu] PC:%04hx fetch %02hhx %s", cpu->cycle, (mos_pa_t)(cpu->PC - 1), opcode, cpu->instr.mnemonic);
}

static inline void latch_address(struct mos6502_cpu* cpu, mos_paddr_t addr)
{
    if (cpu->instr.ctrlbits & MOS_CTRL_PC_ADDRESS_LATCH) {
        cpu->PC = addr;
    } else {
        cpu->eaddr = addr;
    }
    cpu->tstate = MOS_TSTATE_UOP;
}

static inline void retire_instr(struct mos6502_cpu* cpu)
{
    cpu->total_retired++;
    cpu->tstate = MOS_TSTATE_FETCH;
}

static inline void change_flags(struct mos6502_cpu* cpu, mos_word_t mask, mos_word_t val)
{
    cpu->P = (cpu->P & ~mask) | val;
}

static inline void set_value_flags(struct mos6502_cpu* cpu, mos_word_t val)
{
    change_flags(cpu, SR_Z | SR_N, (!val ? SR_Z : 0) | ((val & 0x80) ? SR_N : 0));
}

static void exec_addac(struct mos6502_cpu* cpu, mos_word_t mval)
{
    uint16_t val = cpu->A + mval + !!(cpu->P & SR_C);
    uint8_t res = val & 0xFF;

    /* SR_V is computed using the rule that signed overflow only occures when
       A and M have the same sign and result has a different sign. */
    change_flags(cpu, SR_V, ((cpu->A ^ res) & (mval ^ res) & 0x80) ? SR_V : 0);
    change_flags(cpu, SR_C, val > 0xFF ? SR_C : 0);
    set_value_flags(cpu, res);
    cpu->A = res;
}

static mos_word_t exec_adda3(struct mos6502_cpu* cpu, mos_word_t val1, mos_word_t val2, mos_word_t val3)
{
    uint16_t val = val1 + val2 + val3;
    uint8_t res = val & 0xFF;

    change_flags(cpu, SR_C, val > 0xFF ? SR_C : 0);
    set_value_flags(cpu, res);
    return res;
}

static inline bool is_xpage_ref(mos_paddr_t base, uint8_t offset)
{
    return (~base & 0xFF) < offset;
}

static inline bool should_stall(struct mos6502_cpu *cpu, mos_paddr_t base, uint8_t offset)
{
    /* rw control signal tells us to delay by 1 cycle unconditionally */
    if (cpu->instr.ctrlbits & MOS_CTRL_RW) {
        return true;
    }

    /* insert 1 cycle delay if this is an xpage refrence*/
    if (is_xpage_ref(base, offset)) {
        cpu->instr.ncycles++;
        cpu->instr.ctrlbits |= MOS_CTRL_XPAGE_DELAY;
        return true;
    }

    return false;
}

static void addr_mode_exec(struct mos6502_cpu* cpu)
{
    ep_assert(cpu->tstate == MOS_TSTATE_ADDRESS_LATCH);

    switch (cpu->instr.mode) {
    case MOS_AM_IMM:
        ep_assert(cpu->instr_cycle == 1);
        latch_address(cpu, cpu->PC++);
        break;
    case MOS_AM_Z:
        ep_assert(cpu->instr_cycle == 1);
        latch_address(cpu, cpu_fetch(cpu, cpu->PC++));
        break;
    case MOS_AM_ZX:
        switch (cpu->instr_cycle) {
        case 1:
            cpu->eaddrl = cpu_fetch(cpu, cpu->PC++);
            cpu->eaddrh = 0;
            break;
        case 2:
            /* Discarded read from 00LL */
            cpu_fetch_discard(cpu, cpu->eaddr);
            /* Wrap around on overflow */
            latch_address(cpu, (cpu->eaddr + cpu->X) & 0xFF);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_AM_ZY:
        switch (cpu->instr_cycle) {
        case 1:
            cpu->eaddrl = cpu_fetch(cpu, cpu->PC++);
            cpu->eaddrh = 0;
            break;
        case 2:
            /* Discarded read from 00LL */
            cpu_fetch_discard(cpu, cpu->eaddr);
            /* Wrap around on overflow */
            latch_address(cpu, (cpu->eaddr + cpu->Y) & 0xFF);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_AM_ABS:
        switch (cpu->instr_cycle) {
        case 1:
            cpu->eaddrl = cpu_fetch(cpu, cpu->PC++);
            break;
        case 2:
            cpu->eaddrh = cpu_fetch(cpu, cpu->PC++);
            latch_address(cpu, cpu->eaddr);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_AM_ABSX:
        switch (cpu->instr_cycle) {
        case 1:
            cpu->eaddrl = cpu_fetch(cpu, cpu->PC++);
            break;
        case 2:
            cpu->eaddrh = cpu_fetch(cpu, cpu->PC++);
            if (should_stall(cpu, cpu->eaddr, cpu->X)) {
                break;
            }
            /* fallthru */
        case 3:
            /* Discarded read from HH:(LL+X) on delay cycle */
            if (cpu->instr.ctrlbits & MOS_CTRL_XPAGE_DELAY || cpu->instr.ctrlbits & MOS_CTRL_RW) {
                cpu_fetch_discard(cpu, ((mos_paddr_t)cpu->eaddrh << 8) | ((cpu->eaddrl + cpu->X) & 0xFF));
            }
            latch_address(cpu, cpu->eaddr + cpu->X);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_AM_ABSY:
        switch (cpu->instr_cycle) {
        case 1:
            cpu->eaddrl = cpu_fetch(cpu, cpu->PC++);
            break;
        case 2:
            cpu->eaddrh = cpu_fetch(cpu, cpu->PC++);
            if (should_stall(cpu, cpu->eaddr, cpu->Y)) {
                break;
            }
            /* fallthru */
        case 3:
            /* Discarded read from HH:(LL+Y) on delay cycle */
            if (cpu->instr.ctrlbits & MOS_CTRL_XPAGE_DELAY || cpu->instr.ctrlbits & MOS_CTRL_RW) {
                cpu_fetch_discard(cpu, ((mos_paddr_t)cpu->eaddrh << 8) | ((cpu->eaddrl + cpu->Y) & 0xFF));
            }
            latch_address(cpu, cpu->eaddr + cpu->Y);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_AM_INDX:
        switch (cpu->instr_cycle) {
        case 1:
            /* Read zeropage offset */
            cpu->iaddrl = cpu_fetch(cpu, cpu->PC++);
            cpu->iaddrh = 0;
            break;
        case 2:
            /* Dicarded read from 00LL */
            cpu_fetch_discard(cpu, cpu->iaddr);
            break;
        case 3:
            cpu->eaddrl = cpu_fetch(cpu, (cpu->iaddr + cpu->X) & 0xFF);
            break;
        case 4:
            cpu->eaddrh = cpu_fetch(cpu, (cpu->iaddr + cpu->X + 1) & 0xFF);
            latch_address(cpu, cpu->eaddr);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_AM_INDY:
        switch (cpu->instr_cycle) {
        case 1:
            /* Read zeropage offset */
            cpu->iaddrl = cpu_fetch(cpu, cpu->PC++);
            cpu->iaddrh = 0;
            break;
        case 2:
            cpu->eaddrl = cpu_fetch(cpu, cpu->iaddr++ & 0xFF);
            break;
        case 3:
            cpu->eaddrh = cpu_fetch(cpu, cpu->iaddr++ & 0xFF);
            if (should_stall(cpu, cpu->eaddr, cpu->Y)) {
                break;
            }
            /* fallthru */
        case 4:
            /* Discarded read from HH:(LL+Y) on delay cycle */
            if (cpu->instr.ctrlbits & MOS_CTRL_XPAGE_DELAY || cpu->instr.ctrlbits & MOS_CTRL_RW) {
                cpu_fetch_discard(cpu, ((mos_paddr_t)cpu->eaddrh << 8) | ((cpu->eaddrl + cpu->Y) & 0xFF));
            }
            latch_address(cpu, cpu->eaddr + cpu->Y);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_AM_IND:
        switch (cpu->instr_cycle) {
        case 1:
            cpu->iaddrl = cpu_fetch(cpu, cpu->PC++);
            break;
        case 2:
            cpu->iaddrh = cpu_fetch(cpu, cpu->PC++);
            break;
        case 3:
            cpu->eaddrl = cpu_fetch(cpu, cpu->iaddr++);
            break;
        case 4:
            cpu->eaddrh = cpu_fetch(cpu, cpu->iaddr++);
            latch_address(cpu, cpu->eaddr);
            break;
        default:
            ep_assert(false);
        }
        break;
    default:
        ep_assert(false);
    }
}

static void uop_exec(struct mos6502_cpu* cpu)
{
    ep_assert(!cpu->halted);
    ep_assert(cpu->tstate == MOS_TSTATE_UOP);

    switch (cpu->instr.uop) {
    case MOS_UOP_NOP:
        break;
    case MOS_UOP_LDA:
        cpu->A = cpu_fetch(cpu, cpu->eaddr);
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_LDX:
        cpu->X = cpu_fetch(cpu, cpu->eaddr);
        set_value_flags(cpu, cpu->X);
        break;
    case MOS_UOP_LDY:
        cpu->Y = cpu_fetch(cpu, cpu->eaddr);
        set_value_flags(cpu, cpu->Y);
        break;
    case MOS_UOP_STA:
        cpu_store(cpu, cpu->eaddr, cpu->A);
        break;
    case MOS_UOP_STX:
        cpu_store(cpu, cpu->eaddr, cpu->X);
        break;
    case MOS_UOP_STY:
        cpu_store(cpu, cpu->eaddr, cpu->Y);
        break;
    case MOS_UOP_TAX:
        cpu->X = cpu->A;
        set_value_flags(cpu, cpu->X);
        break;
    case MOS_UOP_TAY:
        cpu->Y = cpu->A;
        set_value_flags(cpu, cpu->Y);
        break;
    case MOS_UOP_TSX:
        cpu->X = cpu->SP;
        set_value_flags(cpu, cpu->X);
        break;
    case MOS_UOP_TXA:
        cpu->A = cpu->X;
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_TXS:
        cpu->SP = cpu->X;
        set_value_flags(cpu, cpu->SP);
        break;
    case MOS_UOP_TYA:
        cpu->A = cpu->Y;
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_PHA:
        switch (cpu->instr_cycle) {
        case 1:
            /* Discarded read at PC+1 */
            cpu_fetch_discard(cpu, cpu->PC);
            break;
        case 2:
            cpu_push(cpu, cpu->A);
            break;
        default:
            ep_assert(false);
        };
        break;
    case MOS_UOP_PLA:
        switch (cpu->instr_cycle) {
        case 1:
            /* Discarded read at PC+1 */
            cpu_fetch_discard(cpu, cpu->PC);
            break;
        case 2:
            /* Discarded read at SP */
            cpu_fetch_discard(cpu, 0x0100 | cpu->SP);
            break;
        case 3:
            cpu->A = cpu_pop(cpu);
            set_value_flags(cpu, cpu->A);
            break;
        default:
            ep_assert(false);
        };
        break;
    case MOS_UOP_PHP:
        switch (cpu->instr_cycle) {
        case 1:
            /* Discarded read at PC+1 */
            cpu_fetch_discard(cpu, cpu->PC);
            break;
        case 2:
            cpu_push(cpu, cpu->P | SR_B | SR_U);
            break;
        default:
            ep_assert(false);
        };
        break;
    case MOS_UOP_PLP:
        switch (cpu->instr_cycle) {
        case 1:
            /* Discarded read at PC+1 */
            cpu_fetch_discard(cpu, cpu->PC);
            break;
        case 2:
            /* Discarded read at SP */
            cpu_fetch_discard(cpu, 0x0100 | cpu->SP);
            break;
        case 3:
            cpu->P = (cpu->P & (SR_B | SR_U)) | (cpu_pop(cpu) & ~(SR_B | SR_U));
            break;
        default:
            ep_assert(false);
        };
        break;
    case MOS_UOP_DEC:
        switch (cpu->instr.ncycles - cpu->instr_cycle) {
        case 3:
            cpu->db = cpu_fetch(cpu, cpu->eaddr);
            break;
        case 2:
            /* Extra write of the current value */
            cpu_store(cpu, cpu->eaddr, cpu->db);
            cpu->db--;
            break;
        case 1:
            cpu_store(cpu, cpu->eaddr, cpu->db);
            set_value_flags(cpu, cpu->db);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_UOP_INC:
        switch (cpu->instr.ncycles - cpu->instr_cycle) {
        case 3:
            cpu->db = cpu_fetch(cpu, cpu->eaddr);
            break;
        case 2:
            /* Extra write of the current value */
            cpu_store(cpu, cpu->eaddr, cpu->db);
            cpu->db++;
            break;
        case 1:
            cpu_store(cpu, cpu->eaddr, cpu->db);
            set_value_flags(cpu, cpu->db);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_UOP_DEX:
        cpu->X = cpu->X - 1;
        set_value_flags(cpu, cpu->X);
        break;
    case MOS_UOP_INX:
        cpu->X = cpu->X + 1;
        set_value_flags(cpu, cpu->X);
        break;
    case MOS_UOP_DEY:
        cpu->Y = cpu->Y - 1;
        set_value_flags(cpu, cpu->Y);
        break;
    case MOS_UOP_INY:
        cpu->Y = cpu->Y + 1;
        set_value_flags(cpu, cpu->Y);
        break;
    case MOS_UOP_ADC:
        exec_addac(cpu, cpu_fetch(cpu, cpu->eaddr));
        break;
    case MOS_UOP_SBC:
        exec_addac(cpu, ~cpu_fetch(cpu, cpu->eaddr));
        break;
    case MOS_UOP_AND:
        cpu->A &= cpu_fetch(cpu, cpu->eaddr);
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_EOR:
        cpu->A ^= cpu_fetch(cpu, cpu->eaddr);
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_ORA:
        cpu->A |= cpu_fetch(cpu, cpu->eaddr);
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_ASL:
        if (cpu->instr.mode == MOS_AM_IMP) {
            change_flags(cpu, SR_C, cpu->A & 0x80 ? SR_C : 0);
            cpu->A <<= 1;
            set_value_flags(cpu, cpu->A);
        } else {
            switch (cpu->instr.ncycles - cpu->instr_cycle) {
            case 3:
                cpu->db = cpu_fetch(cpu, cpu->eaddr);
                break;
            case 2:
                /* Extra write of the current value */
                cpu_store(cpu, cpu->eaddr, cpu->db);
                change_flags(cpu, SR_C, cpu->db & 0x80 ? SR_C : 0);
                cpu->db <<= 1;
                set_value_flags(cpu, cpu->db);
                break;
            case 1:
                cpu_store(cpu, cpu->eaddr, cpu->db);
                break;
            default:
                ep_assert(false);
            }
        }
        break;
    case MOS_UOP_LSR:
        if (cpu->instr.mode == MOS_AM_IMP) {
            change_flags(cpu, SR_C, cpu->A & 0x01 ? SR_C : 0);
            cpu->A >>= 1;
            set_value_flags(cpu, cpu->A);
        } else {
            switch (cpu->instr.ncycles - cpu->instr_cycle) {
            case 3:
                cpu->db = cpu_fetch(cpu, cpu->eaddr);
                break;
            case 2:
                /* Extra write of the current value */
                cpu_store(cpu, cpu->eaddr, cpu->db);
                change_flags(cpu, SR_C, cpu->db & 0x01 ? SR_C : 0);
                cpu->db >>= 1;
                set_value_flags(cpu, cpu->db);
                break;
            case 1:
                cpu_store(cpu, cpu->eaddr, cpu->db);
                break;
            default:
                ep_assert(false);
            }
        }
        break;
    case MOS_UOP_ROL:
        if (cpu->instr.mode == MOS_AM_IMP) {
            bool carry = !!(cpu->P & SR_C);
            change_flags(cpu, SR_C, cpu->A & 0x80 ? SR_C : 0);
            cpu->A = (cpu->A << 1) | carry;
            set_value_flags(cpu, cpu->A);
        } else {
            bool carry;
            switch (cpu->instr.ncycles - cpu->instr_cycle) {
            case 3:
                cpu->db = cpu_fetch(cpu, cpu->eaddr);
                break;
            case 2:
                /* Extra write of the current value */
                cpu_store(cpu, cpu->eaddr, cpu->db);
                carry = !!(cpu->P & SR_C);
                change_flags(cpu, SR_C, cpu->db & 0x80 ? SR_C : 0);
                cpu->db = (cpu->db << 1) | carry;
                set_value_flags(cpu, cpu->db);
                break;
            case 1:
                cpu_store(cpu, cpu->eaddr, cpu->db);
                break;
            default:
                ep_assert(false);
            }
        }
        break;
    case MOS_UOP_ROR:
        if (cpu->instr.mode == MOS_AM_IMP) {
            bool carry = !!(cpu->P & SR_C);
            change_flags(cpu, SR_C, cpu->A & 0x01 ? SR_C : 0);
            cpu->A = (cpu->A >> 1) | (carry << 7);
            set_value_flags(cpu, cpu->A);
        } else {
            bool carry;
            switch (cpu->instr.ncycles - cpu->instr_cycle) {
            case 3:
                cpu->db = cpu_fetch(cpu, cpu->eaddr);
                break;
            case 2:
                /* Extra write of the current value */
                cpu_store(cpu, cpu->eaddr, cpu->db);
                carry = !!(cpu->P & SR_C);
                change_flags(cpu, SR_C, cpu->db & 0x01 ? SR_C : 0);
                cpu->db = (cpu->db >> 1) | (carry << 7);
                set_value_flags(cpu, cpu->db);
                break;
            case 1:
                cpu_store(cpu, cpu->eaddr, cpu->db);
                break;
            default:
                ep_assert(false);
            }
        }
        break;
    case MOS_UOP_CLC:
        cpu->P &= ~SR_C;
        break;
    case MOS_UOP_CLD:
        cpu->P &= ~SR_D;
        break;
    case MOS_UOP_CLI:
        cpu->P &= ~SR_I;
        break;
    case MOS_UOP_CLV:
        cpu->P &= ~SR_V;
        break;
    case MOS_UOP_SEC:
        cpu->P |= SR_C;
        break;
    case MOS_UOP_SED:
        /* BCD is not supported */
        ep_verify(false);
        break;
    case MOS_UOP_SEI:
        cpu->P |= SR_I;
        break;
    case MOS_UOP_BIT:
        cpu->db = cpu_fetch(cpu, cpu->eaddr);
        change_flags(cpu, SR_N | SR_Z | SR_V, (cpu->db == cpu->A ? SR_Z : 0) | (cpu->db & 0xC0));
        break;
    case MOS_UOP_CMP:
        cpu->db = cpu_fetch(cpu, cpu->eaddr);
        cpu->A = exec_adda3(cpu, cpu->A, ~cpu->db, 1);
        break;
    case MOS_UOP_CPX:
        cpu->db = cpu_fetch(cpu, cpu->eaddr);
        cpu->X = exec_adda3(cpu, cpu->X, ~cpu->db, 1);
        break;
    case MOS_UOP_CPY:
        cpu->db = cpu_fetch(cpu, cpu->eaddr);
        cpu->Y = exec_adda3(cpu, cpu->Y, ~cpu->db, 1);
        break;

    #define MOS_EXEC_BR(_cond_) \
        switch (cpu->instr_cycle) { \
        case 1: \
            /* Fetch offset, increment PC, check condition, insert extra cycle if branch taken */ \
            cpu->db = cpu_fetch(cpu, cpu->PC++); \
            if (_cond_) { \
                cpu->instr.ncycles++; \
            } \
            break; \
        case 2: \
            /* Discarded external fetch from PC + 2 */ \
            cpu_fetch_discard(cpu, cpu->PC); \
            /* Branch taken: insert cross-page delay if needed */ \
            if (((cpu->PC + (int8_t)cpu->db) & 0xFF00) != cpu->PCH) { \
                cpu->instr.ncycles++; \
            } \
            cpu->PCL += (int8_t)cpu->db; \
            break; \
        case 3: \
            /* Discarded external fetch from PCH:(PCL+Offset) */ \
            cpu_fetch_discard(cpu, cpu->PC); \
            /* Branch taken: xpage delay cycle */ \
            cpu->PCH += (cpu->db & 0x80 ? -1 : +1); \
            break; \
        default: \
            ep_assert(false); \
        }

    case MOS_UOP_BCC:
        MOS_EXEC_BR(!(cpu->P & SR_C));
        break;
    case MOS_UOP_BCS:
        MOS_EXEC_BR(cpu->P & SR_C);
        break;
    case MOS_UOP_BNE:
        MOS_EXEC_BR(!(cpu->P & SR_Z));
        break;
    case MOS_UOP_BEQ:
        MOS_EXEC_BR(cpu->P & SR_Z);
        break;
    case MOS_UOP_BPL:
        MOS_EXEC_BR(!(cpu->P & SR_N));
        break;
    case MOS_UOP_BMI:
        MOS_EXEC_BR(cpu->P & SR_N);
        break;
    case MOS_UOP_BVC:
        MOS_EXEC_BR(!(cpu->P & SR_V));
        break;
    case MOS_UOP_BVS:
        MOS_EXEC_BR(cpu->P & SR_V);
        break;

    #undef MOS_EXEC_BR

    case MOS_UOP_JMP:
        cpu->PC = cpu->eaddr;
        break;
    case MOS_UOP_JSR:
        /* JSR is not following ordinary ABS addressing mode, so it's hand-coded */
        switch (cpu->instr_cycle) {
        case 1:
            cpu->db = cpu_fetch(cpu, cpu->PC++);
            break;
        case 2:
            /* Stack write stall */
            cpu_fetch_discard(cpu, 0x0100 | cpu->SP);
            break;
        case 3:
            cpu_push(cpu, cpu->PCH);
            break;
        case 4:
            cpu_push(cpu, cpu->PCL);
            break;
        case 5:
            latch_address(cpu, (cpu_fetch(cpu, cpu->PC) << 8) | cpu->db);
            break;
        default:
            ep_assert(false);
        }
        break;
    case MOS_UOP_RTS:
        switch (cpu->instr_cycle) {
        case 1:
            /* Discarded external read from PC + 1 */
            cpu_fetch_discard(cpu, cpu->PC);
            break;
        case 2:
            /* Stack read stall */
            cpu_fetch_discard(cpu, 0x0100 | cpu->SP);
            break;
        case 3:
            cpu->PCL = cpu_pop(cpu);
            break;
        case 4:
            cpu->PCH = cpu_pop(cpu);
            break;
        case 5:
            /* Discarded read from unmodified PC */
            cpu_fetch_discard(cpu, cpu->PC);
            /* Correction for the PC pushed by JSR */
            cpu->PC += 1;
            break;
        default:
            ep_assert(false);
        }
        break;
    default:
        ep_assert(false);
    };
}

struct mos6502_cpu* mos6502_create(void)
{
    return ep_alloc(sizeof(struct mos6502_cpu));
}

void mos6502_init(struct mos6502_cpu* cpu)
{
    ep_verify(cpu != NULL);

    memset(cpu, 0, sizeof(*cpu));
}

void mos6502_reset(struct mos6502_cpu* cpu)
{
    ep_verify(cpu != NULL);

    /* A, X, Y survive reset */
    cpu->PC = (mos_paddr_t)(cpu_fetch(cpu, 0xfffd) << 8) | cpu_fetch(cpu, 0xfffc);
    cpu->SP = 0xfd;
    cpu->P = SR_I | SR_U;
    cpu->halted = false;

    /* It takes 7 cycles to complete the reset sequence. */
    cpu->cycle = 7;
    cpu->total_retired = 0;
    cpu->tstate = MOS_TSTATE_FETCH;

    reset_bus_trace(cpu);
}

mos_word_t mos6502_load_word(struct mos6502_cpu* cpu, mos_paddr_t addr)
{
    ep_verify(cpu);
    return fetch_word(cpu, addr);
}

void mos6502_store_word(struct mos6502_cpu* cpu, mos_paddr_t addr, mos_word_t val)
{
    ep_verify(cpu);
    store_word(cpu, addr, val);
}

mos_word_t mos6502_pop_word(struct mos6502_cpu* cpu)
{
    ep_verify(cpu);
    return pop_word(cpu);
}

void mos6502_push_word(struct mos6502_cpu* cpu, mos_word_t val)
{
    ep_verify(cpu);
    push_word(cpu, val);
}

void mos6502_tick(struct mos6502_cpu* cpu)
{
    ep_verify(cpu != NULL);

    if (cpu->halted) {
        return;
    }

    switch (cpu->tstate) {
    case MOS_TSTATE_FETCH:
        fetch_next_instr(cpu);

        /* Implied addressing mode skips address latch */
        if (cpu->instr.mode == MOS_AM_IMP) {
            cpu->tstate = MOS_TSTATE_UOP;
        } else {
            cpu->tstate = MOS_TSTATE_ADDRESS_LATCH;
        }

        /* HLT instruction is decoded and the cpu is stuck in T1 (fetch) until reset.
           The instruction itself is never retired. */
        if (cpu->instr.uop == MOS_UOP_HLT) {
            cpu->halted = true;
        }

        break;
    case MOS_TSTATE_ADDRESS_LATCH:
        addr_mode_exec(cpu);

        /* Immediate mode does not consume the full cycle */
        if (cpu->instr.mode != MOS_AM_IMM) {
            break;
        }

        /* fallthru */
    case MOS_TSTATE_UOP:
        uop_exec(cpu);
        break;
    default:
        ep_verify(false);
        break;
    }

    cpu->cycle++;
    cpu->instr_cycle++;

    if (!cpu->halted && cpu->instr_cycle == cpu->instr.ncycles) {
        ep_assert(cpu->tstate == MOS_TSTATE_UOP);
        retire_instr(cpu);
    }
}

bool mos6502_is_halted(struct mos6502_cpu* cpu)
{
    ep_verify(cpu != NULL);
    return cpu->halted;
}

uint64_t mos6502_total_retired(struct mos6502_cpu* cpu)
{
    ep_verify(cpu != NULL);
    return cpu->total_retired;
}

uint64_t mos6502_cycles(struct mos6502_cpu* cpu)
{
    ep_verify(cpu != NULL);
    return cpu->cycle;
}

/*
 * Tests.
 */

#ifdef EP_CONFIG_TEST

#include <sys/mman.h>
#include "test.h"

#ifndef MAP_ANON
#  define MAP_ANON 0x20
#endif

#define EP_TEST_RAM_SIZE 0x10000

static mos_word_t test_ram[EP_TEST_RAM_SIZE];
ep_static_assert((mos_paddr_t)-1 < EP_TEST_RAM_SIZE);

mos_word_t* mos6502_decode_paddr(struct mos6502_cpu* cpu, mos_paddr_t paddr)
{
    (void)cpu;

    return &test_ram[paddr];
}

static void init_test_cpu(struct mos6502_cpu* cpu)
{
    memset(test_ram, 0, sizeof(test_ram));
    mos6502_init(cpu);
    mos6502_reset(cpu);
}

static void store_words(struct mos6502_cpu* cpu, uint16_t base, const mos_word_t* data, uint16_t nbytes)
{
    ep_assert((size_t)base + nbytes < EP_TEST_RAM_SIZE);
    memcpy(test_ram + base, data, nbytes);
}

static uint64_t run_test_cpu(struct mos6502_cpu* cpu)
{
    uint64_t cycles = cpu->cycle;
    uint64_t retired = cpu->total_retired;

    /* Run 1 instruction */
    do {
        mos6502_tick(cpu);
    } while (!cpu->halted && cpu->total_retired == retired);

    return (cpu->cycle - cycles);
}

static void validate_bus_trace(struct mos6502_cpu* cpu, const struct mos6502_bus_trace* expected, size_t size)
{
    ep_assert(size <= EP_TEST_BUS_TRACE_SIZE);

    // Expected trace is built in chronological order, while the cpu recorded trace is in reverse.
    uint8_t pos = cpu->bus_trace_head;
    pos = (pos < size ? EP_TEST_BUS_TRACE_SIZE - size + pos : pos - size);
    for (size_t i = 0; i < size; i++, pos++) {
        const struct mos6502_bus_trace* cpu_trace = &cpu->bus_trace[pos % EP_TEST_BUS_TRACE_SIZE];
        ep_verify_equal(expected[i].cycle, cpu_trace->cycle);
        ep_verify_equal(expected[i].addr, cpu_trace->addr);
        ep_verify_equal(!!expected[i].flags.rw, !!cpu_trace->flags.rw);
        ep_verify_equal(!!expected[i].flags.discard, !!cpu_trace->flags.discard);
        ep_verify(!cpu_trace->flags.rw || !cpu_trace->flags.discard);
    }
}

ep_test(test_reset)
{
    struct mos6502_cpu cpu;

    init_test_cpu(&cpu);

    ep_verify_equal(cpu.cycle, 7);
    ep_verify_equal(cpu.PC, 0x0000);
    ep_verify_equal(cpu.SP, 0xfd);
    ep_verify(!(cpu.P & SR_B));
    ep_verify(!(cpu.P & SR_D));
    ep_verify(cpu.P & SR_I);
    ep_verify(!cpu.halted);
}

static void run_hlt_testcase(mos_word_t opcode)
{
    struct mos6502_cpu cpu;
    init_test_cpu(&cpu);
    store_word(&cpu, 0x00, opcode);

    uint64_t cycles = run_test_cpu(&cpu);
    ep_verify(mos6502_is_halted(&cpu));
    ep_verify_equal(cycles, 1);
    ep_verify_equal(cpu.total_retired, 0);
}

ep_test(test_hlt)
{
    run_hlt_testcase(0x02);
    run_hlt_testcase(0x12);
    run_hlt_testcase(0x22);
    run_hlt_testcase(0x32);
    run_hlt_testcase(0x42);
    run_hlt_testcase(0x52);
    run_hlt_testcase(0x62);
    run_hlt_testcase(0x72);
    run_hlt_testcase(0x92);
    run_hlt_testcase(0xb2);
    run_hlt_testcase(0xd2);
    run_hlt_testcase(0xf2);
}

ep_test(test_nop)
{
    struct mos6502_cpu cpu;
    init_test_cpu(&cpu);
    store_word(&cpu, 0x00, 0xea);

    uint64_t cycles = run_test_cpu(&cpu);
    ep_verify_equal(cycles, 2);
    ep_verify_equal(cpu.total_retired, 1);
}

/* Generated tests (see tools/gen_6502_tests.py) */
#include "6502_tests.inc"

#endif
