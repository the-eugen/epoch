/*
 * Ricoh 2A03/2A07 CPU emulation.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "defs.h"

typedef uint8_t     mos_word_t;
typedef uint16_t    mos_pa_t;

enum mos6502_uop
{
    MOS_UOP_NOP,
    MOS_UOP_HLT,
    MOS_UOP_LDA,
    MOS_UOP_LDX,
    MOS_UOP_LDY,
    MOS_UOP_STA,
    MOS_UOP_STX,
    MOS_UOP_STY,
    MOS_UOP_TAX,
    MOS_UOP_TAY,
    MOS_UOP_TSX,
    MOS_UOP_TXA,
    MOS_UOP_TXS,
    MOS_UOP_TYA,
    MOS_UOP_PHA,
    MOS_UOP_PLA,
    MOS_UOP_PHP,
    MOS_UOP_PLP,
    MOS_UOP_DEC,
    MOS_UOP_DEX,
    MOS_UOP_DEY,
    MOS_UOP_INC,
    MOS_UOP_INX,
    MOS_UOP_INY,
    MOS_UOP_ADC,
    MOS_UOP_SBC,
    MOS_UOP_AND,
    MOS_UOP_EOR,
    MOS_UOP_ORA,
    MOS_UOP_ASL,
    MOS_UOP_LSR,
    MOS_UOP_ROL,
    MOS_UOP_ROR,
    MOS_UOP_CLC,
    MOS_UOP_CLD,
    MOS_UOP_CLI,
    MOS_UOP_CLV,
    MOS_UOP_SEC,
    MOS_UOP_SED,
    MOS_UOP_SEI,
};

enum mos6502_addr_mode
{
    MOS_AM_IMP,
    MOS_AM_IMM,
    MOS_AM_Z,
    MOS_AM_ZX,
    MOS_AM_ZY,
    MOS_AM_ABS,
    MOS_AM_ABSX,
    MOS_AM_ABSY,
    MOS_AM_INDX,
    MOS_AM_INDY,
};

struct mos6502_instr
{
    /* Op we're executing */
    uint8_t uop;

    /* Address mode */
    uint8_t mode;

    /* Current instruction cycle, 0-based. */
    uint8_t cycle;

    /* Total cycles this instruction takes to execute */
    uint8_t ncycles;

    /* Execution flags */
    union {
        uint8_t flags;
        /* The instruction address has been latched */
        #define MOS_INSTR_ADDR_LATCHED  (1u << 0)
        /* This intruction can stall for 1 cycle on page cross */
        #define MOS_INSTR_XPAGE_STALL   (1u << 1)
        /* This is a write instruction and will take an extra cycle */
        #define MOS_INSTR_RW  (1u << 2)
        struct {
            uint8_t address_latched:1;
            uint8_t xpage_stall:1;
            uint8_t always_stall:1;
            uint8_t _unused:5;
        };
    };
};
ep_static_assert(sizeof(struct mos6502_instr) <= sizeof(uint64_t));

struct mos6502_cpu;
struct mos6502_pa_range;

typedef void(*mos6502_mmio_handler)(struct mos6502_cpu* cpu,
                                    const struct mos6502_pa_range* range,
                                    bool rw,
                                    mos_pa_t offset,
                                    mos_word_t* pdata);

struct mos6502_pa_range
{
    mos_pa_t base;
    uint32_t size;
    bool is_ram;

    union {
        mos_word_t* mapped;
        mos6502_mmio_handler handler;
    };
};

struct mos6502_cpu
{
    mos_pa_t PC;
    mos_pa_t AB;

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
    mos_word_t DB;

    bool halted;

    struct mos6502_instr instr;
    uint64_t cycle;
    uint64_t total_retired;

    size_t nregions;
    struct mos6502_pa_range pa_map[8];
};

#define _MOS_OP_FLAGS(_opc_, _mnemonic_, _mode_, _ncycles_, _flags_) \
    [(_opc_)] = (struct mos6502_instr) { \
        .uop = MOS_UOP_ ##_mnemonic_, \
        .mode = (_mode_), \
        .ncycles = (_ncycles_), \
        .flags = (_flags_), \
    }

#define _MOS_OP(_opc_, _mnemonic_, _mode_, _ncycles_) \
    _MOS_OP_FLAGS(_opc_, _mnemonic_, _mode_, _ncycles_, 0)

#define _MOS_APPLY_N(_1, _2, _3, _4, _5, _apply, ...) \
    _apply

#define _MOS_APPLY(...) \
    _X(_MOS_APPLY_N(__VA_ARGS__, _MOS_OP_FLAGS, _MOS_OP)(__VA_ARGS__))

#define MOS_OP(...) \
    _X(_MOS_APPLY_N(__VA_ARGS__, _MOS_OP_FLAGS, _MOS_OP)(__VA_ARGS__))

static const struct mos6502_instr mos_opcodes[] =
{
    MOS_OP(0xea, NOP, MOS_AM_IMP,  2),

    MOS_OP(0x02, HLT, MOS_AM_IMP,  1),
    MOS_OP(0x12, HLT, MOS_AM_IMP,  1),
    MOS_OP(0x22, HLT, MOS_AM_IMP,  1),
    MOS_OP(0x32, HLT, MOS_AM_IMP,  1),
    MOS_OP(0x42, HLT, MOS_AM_IMP,  1),
    MOS_OP(0x52, HLT, MOS_AM_IMP,  1),
    MOS_OP(0x62, HLT, MOS_AM_IMP,  1),
    MOS_OP(0x72, HLT, MOS_AM_IMP,  1),
    MOS_OP(0x92, HLT, MOS_AM_IMP,  1),
    MOS_OP(0xb2, HLT, MOS_AM_IMP,  1),
    MOS_OP(0xd2, HLT, MOS_AM_IMP,  1),
    MOS_OP(0xf2, HLT, MOS_AM_IMP,  1),

    MOS_OP(0xa9, LDA, MOS_AM_IMM,  2),
    MOS_OP(0xa5, LDA, MOS_AM_Z,    3),
    MOS_OP(0xb5, LDA, MOS_AM_ZX,   4),
    MOS_OP(0xad, LDA, MOS_AM_ABS,  4),
    MOS_OP(0xbd, LDA, MOS_AM_ABSX, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0xb9, LDA, MOS_AM_ABSY, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0xa1, LDA, MOS_AM_INDX, 6),
    MOS_OP(0xb1, LDA, MOS_AM_INDY, 5, MOS_INSTR_XPAGE_STALL),

    MOS_OP(0xa2, LDX, MOS_AM_IMM,  2),
    MOS_OP(0xa6, LDX, MOS_AM_Z,    3),
    MOS_OP(0xb6, LDX, MOS_AM_ZY,   4),
    MOS_OP(0xae, LDX, MOS_AM_ABS,  4),
    MOS_OP(0xbe, LDX, MOS_AM_ABSY, 4, MOS_INSTR_XPAGE_STALL),

    MOS_OP(0xa0, LDY, MOS_AM_IMM,  2),
    MOS_OP(0xa4, LDY, MOS_AM_Z,    3),
    MOS_OP(0xb4, LDY, MOS_AM_ZX,   4),
    MOS_OP(0xac, LDY, MOS_AM_ABS,  4),
    MOS_OP(0xbc, LDY, MOS_AM_ABSX, 4, MOS_INSTR_XPAGE_STALL),

    MOS_OP(0x85, STA, MOS_AM_Z,    3),
    MOS_OP(0x95, STA, MOS_AM_ZX,   4),
    MOS_OP(0x8D, STA, MOS_AM_ABS,  4),
    MOS_OP(0x9D, STA, MOS_AM_ABSX, 5, MOS_INSTR_RW),
    MOS_OP(0x99, STA, MOS_AM_ABSY, 5, MOS_INSTR_RW),
    MOS_OP(0x81, STA, MOS_AM_INDX, 6),
    MOS_OP(0x91, STA, MOS_AM_INDY, 6, MOS_INSTR_RW),

    MOS_OP(0x86, STX, MOS_AM_Z,    3),
    MOS_OP(0x96, STX, MOS_AM_ZY,   4),
    MOS_OP(0x8E, STX, MOS_AM_ABS,  4),

    MOS_OP(0x84, STY, MOS_AM_Z,    3),
    MOS_OP(0x94, STY, MOS_AM_ZX,   4),
    MOS_OP(0x8C, STY, MOS_AM_ABS,  4),

    MOS_OP(0xAA, TAX, MOS_AM_IMP,  2),
    MOS_OP(0xA8, TAY, MOS_AM_IMP,  2),
    MOS_OP(0xBA, TSX, MOS_AM_IMP,  2),
    MOS_OP(0x8A, TXA, MOS_AM_IMP,  2),
    MOS_OP(0x9A, TXS, MOS_AM_IMP,  2),
    MOS_OP(0x98, TYA, MOS_AM_IMP,  2),

    MOS_OP(0x48, PHA, MOS_AM_IMP,  3),
    MOS_OP(0x68, PLA, MOS_AM_IMP,  4),
    MOS_OP(0x08, PHP, MOS_AM_IMP,  3),
    MOS_OP(0x28, PLP, MOS_AM_IMP,  4),

    MOS_OP(0xC6, DEC, MOS_AM_Z,    5),
    MOS_OP(0xD6, DEC, MOS_AM_ZX,   6),
    MOS_OP(0xCE, DEC, MOS_AM_ABS,  6),
    MOS_OP(0xDE, DEC, MOS_AM_ABSX, 7, MOS_INSTR_RW),
    MOS_OP(0xCA, DEX, MOS_AM_IMP,  2),
    MOS_OP(0x88, DEY, MOS_AM_IMP,  2),

    MOS_OP(0xE6, INC, MOS_AM_Z,    5),
    MOS_OP(0xF6, INC, MOS_AM_ZX,   6),
    MOS_OP(0xEE, INC, MOS_AM_ABS,  6),
    MOS_OP(0xFE, INC, MOS_AM_ABSX, 7, MOS_INSTR_RW),
    MOS_OP(0xE8, INX, MOS_AM_IMP,  2),
    MOS_OP(0xC8, INY, MOS_AM_IMP,  2),

    MOS_OP(0x69, ADC, MOS_AM_IMM,  2),
    MOS_OP(0x65, ADC, MOS_AM_Z,    3),
    MOS_OP(0x75, ADC, MOS_AM_ZX,   4),
    MOS_OP(0x6D, ADC, MOS_AM_ABS,  4),
    MOS_OP(0x7D, ADC, MOS_AM_ABSX, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0x79, ADC, MOS_AM_ABSY, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0x61, ADC, MOS_AM_INDX, 6),
    MOS_OP(0x71, ADC, MOS_AM_INDY, 5, MOS_INSTR_XPAGE_STALL),

    MOS_OP(0xE9, SBC, MOS_AM_IMM,  2),
    MOS_OP(0xE5, SBC, MOS_AM_Z,    3),
    MOS_OP(0xF5, SBC, MOS_AM_ZX,   4),
    MOS_OP(0xED, SBC, MOS_AM_ABS,  4),
    MOS_OP(0xFD, SBC, MOS_AM_ABSX, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0xF9, SBC, MOS_AM_ABSY, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0xE1, SBC, MOS_AM_INDX, 6),
    MOS_OP(0xF1, SBC, MOS_AM_INDY, 5, MOS_INSTR_XPAGE_STALL),

    MOS_OP(0x29, AND, MOS_AM_IMM,  2),
    MOS_OP(0x25, AND, MOS_AM_Z,    3),
    MOS_OP(0x35, AND, MOS_AM_ZX,   4),
    MOS_OP(0x2D, AND, MOS_AM_ABS,  4),
    MOS_OP(0x3D, AND, MOS_AM_ABSX, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0x39, AND, MOS_AM_ABSY, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0x21, AND, MOS_AM_INDX, 6),
    MOS_OP(0x31, AND, MOS_AM_INDY, 5, MOS_INSTR_XPAGE_STALL),

    MOS_OP(0x49, EOR, MOS_AM_IMM,  2),
    MOS_OP(0x45, EOR, MOS_AM_Z,    3),
    MOS_OP(0x55, EOR, MOS_AM_ZX,   4),
    MOS_OP(0x4D, EOR, MOS_AM_ABS,  4),
    MOS_OP(0x5D, EOR, MOS_AM_ABSX, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0x59, EOR, MOS_AM_ABSY, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0x41, EOR, MOS_AM_INDX, 6),
    MOS_OP(0x51, EOR, MOS_AM_INDY, 5, MOS_INSTR_XPAGE_STALL),

    MOS_OP(0x09, ORA, MOS_AM_IMM,  2),
    MOS_OP(0x05, ORA, MOS_AM_Z,    3),
    MOS_OP(0x15, ORA, MOS_AM_ZX,   4),
    MOS_OP(0x0D, ORA, MOS_AM_ABS,  4),
    MOS_OP(0x1D, ORA, MOS_AM_ABSX, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0x19, ORA, MOS_AM_ABSY, 4, MOS_INSTR_XPAGE_STALL),
    MOS_OP(0x01, ORA, MOS_AM_INDX, 6),
    MOS_OP(0x11, ORA, MOS_AM_INDY, 5, MOS_INSTR_XPAGE_STALL),

    MOS_OP(0x0A, ASL, MOS_AM_IMP,  2),
    MOS_OP(0x06, ASL, MOS_AM_Z,    5),
    MOS_OP(0x16, ASL, MOS_AM_ZX,   6),
    MOS_OP(0x0E, ASL, MOS_AM_ABS,  6),
    MOS_OP(0x1E, ASL, MOS_AM_ABSX, 7, MOS_INSTR_RW),

    MOS_OP(0x4A, LSR, MOS_AM_IMP,  2),
    MOS_OP(0x46, LSR, MOS_AM_Z,    5),
    MOS_OP(0x56, LSR, MOS_AM_ZX,   6),
    MOS_OP(0x4E, LSR, MOS_AM_ABS,  6),
    MOS_OP(0x5E, LSR, MOS_AM_ABSX, 7, MOS_INSTR_RW),

    MOS_OP(0x2A, ROL, MOS_AM_IMP,  2),
    MOS_OP(0x26, ROL, MOS_AM_Z,    5),
    MOS_OP(0x36, ROL, MOS_AM_ZX,   6),
    MOS_OP(0x2E, ROL, MOS_AM_ABS,  6),
    MOS_OP(0x3E, ROL, MOS_AM_ABSX, 7, MOS_INSTR_RW),

    MOS_OP(0x6A, ROR, MOS_AM_IMP,  2),
    MOS_OP(0x66, ROR, MOS_AM_Z,    5),
    MOS_OP(0x76, ROR, MOS_AM_ZX,   6),
    MOS_OP(0x6E, ROR, MOS_AM_ABS,  6),
    MOS_OP(0x7E, ROR, MOS_AM_ABSX, 7, MOS_INSTR_RW),

    MOS_OP(0x18, CLC, MOS_AM_IMP,  2),
    MOS_OP(0xD8, CLD, MOS_AM_IMP,  2),
    MOS_OP(0x58, CLI, MOS_AM_IMP,  2),
    MOS_OP(0xB8, CLV, MOS_AM_IMP,  2),
    MOS_OP(0x38, SEC, MOS_AM_IMP,  2),
    MOS_OP(0xF8, SED, MOS_AM_IMP,  2),
    MOS_OP(0x78, SEI, MOS_AM_IMP,  2),
};

static const struct mos6502_pa_range* map_addr(struct mos6502_cpu* cpu, mos_pa_t pa)
{
    /* TODO: this can be a binary search, but the amount of regions is small enough for now */
    for (size_t i = 0; i < cpu->nregions; i++) {
        struct mos6502_pa_range* r = &cpu->pa_map[i];
        if (r->base <= pa && pa <= r->base + (r->size - 1)) {
            return r;
        }
    }

    /* TODO: Handle unmapped addresses */
    ep_verify(false);
}

static mos_word_t load_word(struct mos6502_cpu* cpu, mos_pa_t pa)
{
    const struct mos6502_pa_range* r = map_addr(cpu, pa);
    ep_verify(r);

    mos_word_t val;
    if (r->is_ram) {
        val = r->mapped[pa - r->base];
    } else {
        r->handler(cpu, r, false, pa - r->base, &val);
    }

    return val;
}

static void store_word(struct mos6502_cpu* cpu, mos_pa_t pa, mos_word_t val)
{
    const struct mos6502_pa_range* r = map_addr(cpu, pa);
    ep_verify(r);

    if (r->is_ram) {
        r->mapped[pa - r->base] = val;
    } else {
        r->handler(cpu, r, true, pa - r->base, &val);
    }
}

static struct mos6502_instr fetch_next_instr(struct mos6502_cpu* cpu)
{
    /* TODO: unimplemented instruction exception? */
    return mos_opcodes[load_word(cpu, cpu->PC++)];
}

static void latch_address(struct mos6502_cpu* cpu, mos_pa_t addr)
{
    cpu->AB = addr;
    cpu->instr.address_latched = true;
}

static inline bool instr_is_tplus(struct mos6502_instr instr)
{
    /* HLT never has a T+ stage */
    return (instr.uop != MOS_UOP_HLT) && (instr.cycle + 1 == instr.ncycles);
}

static inline bool instr_should_stall(struct mos6502_instr* instr, mos_pa_t base, mos_word_t index)
{
    if (instr->always_stall) {
        return true;
    }

    if ((~base & 0xFF) < index && instr->xpage_stall) {
        /* Insert a 1 cycle delay on page crossing without completing the uop */
        instr->ncycles++;
        return true;
    }

    return false;
}

static inline void change_flags(struct mos6502_cpu* cpu, mos_word_t mask, mos_word_t val)
{
    cpu->P = (cpu->P & ~mask) | val;
}

static inline void set_value_flags(struct mos6502_cpu* cpu, mos_word_t val)
{
    change_flags(cpu, SR_Z | SR_N, (!val ? SR_Z : 0) | ((val & 0x80) ? SR_N : 0));
}

static void exec_addc(struct mos6502_cpu* cpu, mos_word_t mval)
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

static bool addr_mode_exec(struct mos6502_cpu* cpu)
{
    bool immediate = false;

    switch (cpu->instr.mode) {
    case MOS_AM_IMP:
        /* Do nothing */
        ep_verify(cpu->instr.cycle == 0);
        cpu->instr.address_latched = true;
        immediate = true;
        break;
    case MOS_AM_IMM:
        /* Immediate mode does not consume the full cycle */
        ep_verify(cpu->instr.cycle == 0);
        latch_address(cpu, cpu->PC++);
        immediate = true;
        break;
    case MOS_AM_Z:
        ep_verify(cpu->instr.cycle == 0);
        latch_address(cpu, load_word(cpu, cpu->PC++));
        break;
    case MOS_AM_ZX:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->DB = load_word(cpu, cpu->PC++);
            break;
        case 1:
            /* Wrap around on overflow */
            latch_address(cpu, (cpu->DB + cpu->X) & 0xFF);
            break;
        default:
            ep_verify(false);
        }
        break;
    case MOS_AM_ZY:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->DB = load_word(cpu, cpu->PC++);
            break;
        case 1:
            /* Wrap around on overflow */
            latch_address(cpu, (cpu->DB + cpu->Y) & 0xFF);
            break;
        default:
            ep_verify(false);
        }
        break;
    case MOS_AM_ABS:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->AB = load_word(cpu, cpu->PC++);
            break;
        case 1:
            latch_address(cpu, ((mos_pa_t)load_word(cpu, cpu->PC++) << 8) | cpu->AB);
            break;
        default:
            ep_verify(false);
        }
        break;
    case MOS_AM_ABSX:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->AB = load_word(cpu, cpu->PC++);
            break;
        case 1:
            cpu->AB = ((mos_pa_t)load_word(cpu, cpu->PC++) << 8) | cpu->AB;
            if (instr_should_stall(&cpu->instr, cpu->AB, cpu->X)) {
                break;
            }
            /* fallthru */
        case 2: /* delay cycle */
            latch_address(cpu, cpu->AB + cpu->X);
            break;
        default:
            ep_verify(false);
        }
        break;
    case MOS_AM_ABSY:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->AB = load_word(cpu, cpu->PC++);
            break;
        case 1:
            cpu->AB = ((mos_pa_t)load_word(cpu, cpu->PC++) << 8) | cpu->AB;
            if (instr_should_stall(&cpu->instr, cpu->AB, cpu->Y)) {
                break;
            }
            /* fallthru */
        case 2: /* delay cycle */
            latch_address(cpu, cpu->AB + cpu->Y);
            break;
        default:
            ep_verify(false);
        }
        break;
    case MOS_AM_INDX:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->DB = load_word(cpu, cpu->PC++);
            break;
        case 1:
            /* Address might wrap around which is what we want here */
            cpu->DB += cpu->X;
            break;
        case 2:
            cpu->AB = load_word(cpu, cpu->DB++);
            break;
        case 3:
            latch_address(cpu, ((mos_pa_t)load_word(cpu, cpu->DB) << 8) | cpu->AB);
            break;
        default:
            ep_verify(false);
        }
        break;
    case MOS_AM_INDY:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->DB = load_word(cpu, cpu->PC++);
            break;
        case 1:
            cpu->AB = load_word(cpu, cpu->DB++);
            break;
        case 2:
            cpu->AB = ((mos_pa_t)load_word(cpu, cpu->DB) << 8) | cpu->AB;
            if (instr_should_stall(&cpu->instr, cpu->AB, cpu->Y)) {
                break;
            }
            /* fallthru */
        case 3: /* delay cycle */
            latch_address(cpu, cpu->AB + cpu->Y);
            break;
        default:
            ep_verify(false);
        }
        break;
    default:
        ep_verify(false);
    }

    return immediate;
}

static void uop_exec(struct mos6502_cpu* cpu)
{
    ep_verify(!cpu->halted);

    switch (cpu->instr.uop) {
    case MOS_UOP_NOP:
        break;
    case MOS_UOP_HLT:
        cpu->halted = true;
        break;
    case MOS_UOP_LDA:
        assert(cpu->instr.address_latched);
        cpu->A = load_word(cpu, cpu->AB);
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_LDX:
        assert(cpu->instr.address_latched);
        cpu->X = load_word(cpu, cpu->AB);
        set_value_flags(cpu, cpu->X);
        break;
    case MOS_UOP_LDY:
        assert(cpu->instr.address_latched);
        cpu->Y = load_word(cpu, cpu->AB);
        set_value_flags(cpu, cpu->Y);
        break;
    case MOS_UOP_STA:
        assert(cpu->instr.address_latched);
        store_word(cpu, cpu->AB, cpu->A);
        break;
    case MOS_UOP_STX:
        assert(cpu->instr.address_latched);
        store_word(cpu, cpu->AB, cpu->X);
        break;
    case MOS_UOP_STY:
        assert(cpu->instr.address_latched);
        store_word(cpu, cpu->AB, cpu->Y);
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
        switch (cpu->instr.cycle) {
        case 0:
            cpu->AB = 0x0100 | cpu->SP;
            break;
        case 1:
            store_word(cpu, cpu->AB, cpu->A);
            cpu->SP--;
            break;
        default:
            ep_verify(false);
        };
        break;
    case MOS_UOP_PLA:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->SP++;
            break;
        case 1:
            cpu->AB = 0x0100 | cpu->SP;
            break;
        case 2:
            cpu->A = load_word(cpu, cpu->AB);
            set_value_flags(cpu, cpu->A);
            break;
        default:
            ep_verify(false);
        };
        break;
    case MOS_UOP_PHP:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->AB = 0x0100 | cpu->SP;
            break;
        case 1:
            store_word(cpu, cpu->AB, cpu->P | SR_B | SR_U);
            cpu->SP--;
            break;
        default:
            ep_verify(false);
        };
        break;
    case MOS_UOP_PLP:
        switch (cpu->instr.cycle) {
        case 0:
            cpu->SP++;
            break;
        case 1:
            cpu->AB = 0x0100 | cpu->SP;
            break;
        case 2:
            cpu->P = (cpu->P & (SR_B | SR_U)) | (load_word(cpu, cpu->AB) & ~(SR_B | SR_U));
            break;
        default:
            ep_verify(false);
        };
        break;
    case MOS_UOP_DEC:
        assert(cpu->instr.address_latched);
        switch (cpu->instr.ncycles - cpu->instr.cycle - 1) {
        case 3:
            cpu->DB = load_word(cpu, cpu->AB);
            break;
        case 2:
            cpu->DB--;
            break;
        case 1:
            store_word(cpu, cpu->AB, cpu->DB);
            set_value_flags(cpu, cpu->DB);
            break;
        default:
            ep_verify(false);
        }
        break;
    case MOS_UOP_INC:
        assert(cpu->instr.address_latched);
        switch (cpu->instr.ncycles - cpu->instr.cycle - 1) {
        case 3:
            cpu->DB = load_word(cpu, cpu->AB);
            break;
        case 2:
            cpu->DB++;
            break;
        case 1:
            store_word(cpu, cpu->AB, cpu->DB);
            set_value_flags(cpu, cpu->DB);
            break;
        default:
            ep_verify(false);
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
        assert(cpu->instr.address_latched);
        exec_addc(cpu, load_word(cpu, cpu->AB));
        break;
    case MOS_UOP_SBC:
        assert(cpu->instr.address_latched);
        exec_addc(cpu, ~load_word(cpu, cpu->AB));
        break;
    case MOS_UOP_AND:
        assert(cpu->instr.address_latched);
        cpu->A &= load_word(cpu, cpu->AB);
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_EOR:
        assert(cpu->instr.address_latched);
        cpu->A ^= load_word(cpu, cpu->AB);
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_ORA:
        assert(cpu->instr.address_latched);
        cpu->A |= load_word(cpu, cpu->AB);
        set_value_flags(cpu, cpu->A);
        break;
    case MOS_UOP_ASL:
        if (cpu->instr.mode == MOS_AM_IMP) {
            change_flags(cpu, SR_C, cpu->A & 0x80 ? SR_C : 0);
            cpu->A <<= 1;
            set_value_flags(cpu, cpu->A);
        } else {
            assert(cpu->instr.address_latched);
            switch (cpu->instr.ncycles - cpu->instr.cycle - 1) {
            case 3:
                cpu->DB = load_word(cpu, cpu->AB);
                break;
            case 2:
                change_flags(cpu, SR_C, cpu->DB & 0x80 ? SR_C : 0);
                cpu->DB <<= 1;
                set_value_flags(cpu, cpu->DB);
                break;
            case 1:
                store_word(cpu, cpu->AB, cpu->DB);
                break;
            default:
                ep_verify(false);
            }
        }
        break;
    case MOS_UOP_LSR:
        if (cpu->instr.mode == MOS_AM_IMP) {
            change_flags(cpu, SR_C, cpu->A & 0x01 ? SR_C : 0);
            cpu->A >>= 1;
            set_value_flags(cpu, cpu->A);
        } else {
            assert(cpu->instr.address_latched);
            switch (cpu->instr.ncycles - cpu->instr.cycle - 1) {
            case 3:
                cpu->DB = load_word(cpu, cpu->AB);
                break;
            case 2:
                change_flags(cpu, SR_C, cpu->DB & 0x01 ? SR_C : 0);
                cpu->DB >>= 1;
                set_value_flags(cpu, cpu->DB);
                break;
            case 1:
                store_word(cpu, cpu->AB, cpu->DB);
                break;
            default:
                ep_verify(false);
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
            assert(cpu->instr.address_latched);
            switch (cpu->instr.ncycles - cpu->instr.cycle - 1) {
            case 3:
                cpu->DB = load_word(cpu, cpu->AB);
                break;
            case 2:
                carry = !!(cpu->P & SR_C);
                change_flags(cpu, SR_C, cpu->DB & 0x80 ? SR_C : 0);
                cpu->DB = (cpu->DB << 1) | carry;
                set_value_flags(cpu, cpu->DB);
                break;
            case 1:
                store_word(cpu, cpu->AB, cpu->DB);
                break;
            default:
                ep_verify(false);
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
            assert(cpu->instr.address_latched);
            switch (cpu->instr.ncycles - cpu->instr.cycle - 1) {
            case 3:
                cpu->DB = load_word(cpu, cpu->AB);
                break;
            case 2:
                carry = !!(cpu->P & SR_C);
                change_flags(cpu, SR_C, cpu->DB & 0x01 ? SR_C : 0);
                cpu->DB = (cpu->DB >> 1) | (carry << 7);
                set_value_flags(cpu, cpu->DB);
                break;
            case 1:
                store_word(cpu, cpu->AB, cpu->DB);
                break;
            default:
                ep_verify(false);
            }
        }
        break;
    case MOS_UOP_CLC:
        cpu->P &= ~SR_C;
        break;
    case MOS_UOP_CLD:
        /* BCD is not supported */
        ep_verify(false);
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
    default:
        ep_verify(false);
    };
}

static void insert_pa_range(struct mos6502_cpu* cpu, struct mos6502_pa_range* range)
{
    ep_verify(cpu->nregions < sizeof(cpu->pa_map) / sizeof(cpu->pa_map[0]));

    /* Find a place to insert the region into */
    for (size_t i = 0; i < cpu->nregions; i++) {
        if (cpu->pa_map[i].base > range->base) {
            ep_verify(range->base + (range->size - 1) < cpu->pa_map[i].base);
            memmove(&cpu->pa_map[i + 1], &cpu->pa_map[i], sizeof(*range) * (cpu->nregions - i));
            cpu->pa_map[i] = *range;
            cpu->nregions++;
            return;
        }

        ep_verify(cpu->pa_map[i].base + (cpu->pa_map[i].size - 1) < range->base);
    }

    /* No place found, insert at the end */
    cpu->pa_map[cpu->nregions++] = *range;
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
    cpu->PC = (mos_pa_t)(load_word(cpu, 0xfffd) << 8) | load_word(cpu, 0xfffc);
    cpu->SP = 0xfd;
    cpu->P = SR_I | SR_U;
    cpu->halted = false;

    /* It takes 8 cycles to complete the reset and fetch the first opcode.
       It's not exactly cycle-precice here, since we're skipping a lot and going straight to 8. */
    cpu->cycle = 8;
    cpu->total_retired = 0;
    cpu->instr = fetch_next_instr(cpu);
}

void mos6502_map_ram_region(struct mos6502_cpu* cpu, mos_pa_t base, size_t size, void* mapped)
{
    ep_verify(cpu != NULL);
    ep_verify(mapped != NULL);
    ep_verify(size != 0);
    ep_verify((~base & 0xFFFF) <= size - 1);

    struct mos6502_pa_range range = {
        .base = base,
        .size = size,
        .is_ram = true,
        .mapped = mapped,
    };

    insert_pa_range(cpu, &range);
}

mos_word_t mos6502_load_word(struct mos6502_cpu* cpu, mos_pa_t addr)
{
    ep_verify(cpu);
    return load_word(cpu, addr);
}

void mos6502_store_word(struct mos6502_cpu* cpu, mos_pa_t addr, mos_word_t val)
{
    ep_verify(cpu);
    return store_word(cpu, addr, val);
}

bool mos6502_tick(struct mos6502_cpu* cpu)
{
    ep_verify(cpu != NULL);

    bool retired = false;

    if (cpu->halted) {
        return false;
    }

    if (!cpu->instr.address_latched && !addr_mode_exec(cpu)) {
        goto cycle_done;
    }

    ep_verify(cpu->instr.address_latched);
    if (!instr_is_tplus(cpu->instr)) {
        uop_exec(cpu);
        if (cpu->halted) {
            goto retire;
        }
        goto cycle_done;
    }

    ep_verify(instr_is_tplus(cpu->instr));
    if (!cpu->halted) {
        cpu->instr = fetch_next_instr(cpu);
    }

retire:
    cpu->total_retired++;
    retired = true;

cycle_done:
    cpu->cycle++;
    if (!retired) {
        cpu->instr.cycle++;
    }

    return retired;
}

bool mos6502_is_halted(struct mos6502_cpu* cpu)
{
    ep_verify(cpu != NULL);
    return cpu->halted;
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

static void* alloc_ram_region(void)
{
    mos_word_t* ram = (mos_word_t*)mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    ep_verify(ram != MAP_FAILED);
    memset(ram, 0, 0x10000);

    return ram;
}

static void free_ram_region(void* ram)
{
    if (ram) {
        munmap(ram, 0x10000);
    }
}

struct test_ram_segment
{
    mos_pa_t offset;
    mos_pa_t size;
    const mos_word_t* data;
};

#define MAKE_TEST_SEGMENT(_offset_, ...) \
    { (_offset_), sizeof((const mos_word_t[]){ __VA_ARGS__ }), (const mos_word_t[]){ __VA_ARGS__ } }

/* This version allows specifying data as {a, b} vector */
#define MAKE_TEST_SEGMENT_VEC(_offset_, ...) \
    { (_offset_), sizeof((const mos_word_t[]) __VA_ARGS__ ), (const mos_word_t[]) __VA_ARGS__ }

static void init_test_cpu(struct mos6502_cpu* cpu, const struct test_ram_segment* segments, size_t nsegments)
{
    mos_word_t* ram = alloc_ram_region();

    /* Setup the reset vector and opcodes */
    for (size_t i = 0; i < nsegments; i++) {
        memcpy(ram + segments[i].offset, segments[i].data, segments[i].size);
    }
    ram[0xFFFC] = 0x00;
    ram[0xFFFD] = 0x00;

    mos6502_init(cpu);
    mos6502_map_ram_region(cpu, 0, 0x10000, ram);
    mos6502_reset(cpu);
}

static void free_test_cpu(struct mos6502_cpu* cpu)
{
    // TODO
}

static uint64_t run_test_cpu(struct mos6502_cpu* cpu)
{
    /* We expect each test to terminate with a HLT */
    uint64_t cycles = cpu->cycle;
    while (!mos6502_is_halted(cpu)) {
        mos6502_tick(cpu);
    }

    /* Subtract 1 cycle for the HLT */
    return (cpu->cycle - cycles);
}

static uint64_t run_opcode(struct mos6502_cpu* cpu, mos_word_t opcode)
{
    struct test_ram_segment seg = MAKE_TEST_SEGMENT(0, opcode);
    init_test_cpu(cpu, &seg, 1);

    uint64_t cycles = run_test_cpu(cpu);
    free_test_cpu(cpu);

    return cycles;
}

static void run_hlt_testcase(mos_word_t opcode)
{
    struct mos6502_cpu cpu;
    uint64_t cycles = run_opcode(&cpu, opcode);
    ep_verify(mos6502_is_halted(&cpu));
    ep_verify_equal(cycles, 1);
    ep_verify_equal(cpu.total_retired, 1);
}

ep_test(test_reset)
{
    struct mos6502_cpu cpu;

    mos_word_t* ram = alloc_ram_region();
    ram[0xFFFC] = 0x00;
    ram[0xFFFD] = 0x00;
    ram[0x0000] = 0xea; /* NOP, something for the reset to fetch */

    mos6502_init(&cpu);
    mos6502_map_ram_region(&cpu, 0, 0x10000, ram);
    mos6502_reset(&cpu);

    ep_verify_equal(cpu.cycle, 8);
    ep_verify_equal(cpu.PC, 0x0001);
    ep_verify_equal(cpu.SP, 0xfd);
    ep_verify(!(cpu.P & SR_B));
    ep_verify(!(cpu.P & SR_D));
    ep_verify(cpu.P & SR_I);
    ep_verify(!cpu.halted);

    free_ram_region(ram);
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
    struct test_ram_segment segment = MAKE_TEST_SEGMENT_VEC(0x0, {0xea, 0x02});
    init_test_cpu(&cpu, &segment, 1);

    uint64_t cycles = run_test_cpu(&cpu) - 1;
    ep_verify_equal(cycles, 2);
    ep_verify_equal(cpu.total_retired, 2);

    free_test_cpu(&cpu);
}

#include "6502_tests.inc"

#endif
