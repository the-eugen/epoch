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
    MOS_UOP_LDA,
    MOS_UOP_LDX,
    MOS_UOP_LDY,
    MOS_UOP_HLT,
};

enum mos6502_addr_mode
{
    MOS_AM_IMPLIED,
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

    /* Total cycles this instruction will take.
     * Might change during execution due to various address bus delays we need to emulate. */
    uint8_t ncycles;

    /* Current instruction cycle, 0-based. */
    uint8_t cycle;

    /* Execution flags */
    struct {
        uint8_t address_latched:1;
        uint8_t _unused:7;
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
    mos_word_t S;
    mos_word_t DB;

    bool halted;

    struct mos6502_instr instr;
    uint64_t cycle;
    uint64_t total_retired;

    size_t nregions;
    struct mos6502_pa_range pa_map[8];
};

#define _X(x) x

#define _UOPS1(_uop1_) \
    ((uint32_t)(_uop1_))

#define _UOPS2(_uop1_, _uop2_) \
    (((uint32_t)(_uop1_) << 4) | (_uop2_))

#define _UOPS3(_uop1_, _uop2_, _uop3_) \
    (((uint32_t)(_uop1_) << 8) | ((_uop2_) << 4) | (_uop3_))

#define _UOPS4(_uop1_, _uop2_, _uop3_, _uop4_) \
    (((uint32_t)(_uop1_) << 12) | ((_uop2_) << 8) | ((_uop3_) << 4) | (_uop4_))

#define _UOPS5(_uop1_, _uop2_, _uop3_, _uop4_, _uop5_) \
    (((uint32_t)(_uop1_) << 16) | ((_uop2_) << 12) | ((_uop3_) << 8) | ((_uop4_) << 4) | (_uop5_))

#define _UOPS_APPLY_N(_1, _2, _3, _4, _5, _uops_n_, ...) \
    _uops_n_

#define _UOPS_APPLY(...) \
    _X(_UOPS_APPLY_N(__VA_ARGS__, _UOPS5, _UOPS4, _UOPS3, _UOPS2, _UOPS1)(__VA_ARGS__))

#define MOS_OP(_opc_, _mnemonic_, _mode_, _cycles_, ...) \
    [(_opc_)] = (struct mos6502_instr) { \
        /*.uops = (_UOPS_APPLY(__VA_ARGS__) << 4) | MOS_UOP_FETCH_NEXT,*/  \
        .uop = MOS_UOP_ ##_mnemonic_, \
        .mode = (_mode_), \
        .ncycles = (_cycles_), \
    }

static const struct mos6502_instr mos_opcodes[] =
{
    MOS_OP(0xea, NOP, MOS_AM_IMPLIED, 2, MOS_UOP_NOP),

    MOS_OP(0x02, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0x12, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0x22, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0x32, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0x42, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0x52, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0x62, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0x72, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0x92, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0xb2, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0xd2, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),
    MOS_OP(0xf2, HLT, MOS_AM_IMPLIED, 1, MOS_UOP_HLT),

    MOS_OP(0xa9, LDA, MOS_AM_IMM,  2, MOS_UOP_LDA),
    MOS_OP(0xa5, LDA, MOS_AM_Z,    3, MOS_UOP_LDA),
    MOS_OP(0xb5, LDA, MOS_AM_ZX,   4, MOS_UOP_LDA),
    MOS_OP(0xad, LDA, MOS_AM_ABS,  4, MOS_UOP_LDA),
    MOS_OP(0xbd, LDA, MOS_AM_ABSX, 4, MOS_UOP_LDA),
    MOS_OP(0xb9, LDA, MOS_AM_ABSY, 4, MOS_UOP_LDA),
    MOS_OP(0xa1, LDA, MOS_AM_INDX, 6, MOS_UOP_LDA),
    MOS_OP(0xb1, LDA, MOS_AM_INDY, 5, MOS_UOP_LDA),

    MOS_OP(0xa2, LDX, MOS_AM_IMM,  2, MOS_UOP_LDX),
    MOS_OP(0xa6, LDX, MOS_AM_Z,    3, MOS_UOP_LDX),
    MOS_OP(0xb6, LDX, MOS_AM_ZY,   4, MOS_UOP_LDX),
    MOS_OP(0xae, LDX, MOS_AM_ABS,  4, MOS_UOP_LDX),
    MOS_OP(0xbe, LDX, MOS_AM_ABSY, 4, MOS_UOP_LDX),

    MOS_OP(0xa0, LDY, MOS_AM_IMM,  2, MOS_UOP_LDY),
    MOS_OP(0xa4, LDY, MOS_AM_Z,    3, MOS_UOP_LDY),
    MOS_OP(0xb4, LDY, MOS_AM_ZX,   4, MOS_UOP_LDY),
    MOS_OP(0xac, LDY, MOS_AM_ABS,  4, MOS_UOP_LDY),
    MOS_OP(0xbc, LDY, MOS_AM_ABSX, 4, MOS_UOP_LDY),
};

static const struct mos6502_pa_range* map_addr(struct mos6502_cpu* cpu, mos_pa_t pa)
{
    /* TODO: this can be a binary search, but the amount of regions is small enough for now */
    for (size_t i = 0; i < cpu->nregions; i++) {
        struct mos6502_pa_range* r = &cpu->pa_map[i];
        if (r->base <= pa && pa <= r->base + (r->size - 1)) {
            mos_word_t res;
            if (r->is_ram) {
                res = r->mapped[pa - r->base];
            } else {
                r->handler(cpu, r, false, pa - r->base, &res);
            }

            return res;
        }
    }

    /* TODO: Handle unmapped addresses */
    ep_verify(false);
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

static bool addr_mode_exec(struct mos6502_cpu* cpu)
{
    bool immediate = false;

    switch (cpu->instr.mode) {
    case MOS_AM_IMPLIED:
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
        /* Wrap around on overflow */
        ep_verify(cpu->instr.cycle == 0);
        latch_address(cpu, (load_word(cpu, cpu->PC++) + cpu->Y) & 0xFF);
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
            if ((~cpu->AB & 0xFF) < cpu->X) {
                /* Insert a 1 cycle delay on page crossing without completing the uop */
                cpu->instr.ncycles += 1;
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
            if ((~cpu->AB & 0xFF) < cpu->Y) {
                /* Insert a 1 cycle delay on page crossing without completing the uop */
                cpu->instr.ncycles += 1;
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
            if ((~cpu->AB & 0xFF) < cpu->Y) {
                /* Insert a 1 cycle delay on page crossing without completing the uop */
                cpu->instr.ncycles += 1;
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

    #define MOS_REG(_reg_) cpu->_reg_
    #define MOS_STORE_REG(_reg_, _val_) \
        MOS_REG(_reg_) = (_val_); \
        cpu->P |= (!MOS_REG(_reg_) ? SR_Z : 0) | ((MOS_REG(_reg_) & 0x80) ? SR_N : 0); \

    switch (cpu->instr.uop) {
    case MOS_UOP_NOP:
        break;
    case MOS_UOP_HLT:
        cpu->halted = true;
        break;
    case MOS_UOP_LDA:
        assert(cpu->instr.address_latched);
        MOS_STORE_REG(A, load_word(cpu, cpu->AB));
        break;
    case MOS_UOP_LDX:
        assert(cpu->instr.address_latched);
        MOS_STORE_REG(X, load_word(cpu, cpu->AB));
        break;
    case MOS_UOP_LDY:
        assert(cpu->instr.address_latched);
        MOS_STORE_REG(Y, load_word(cpu, cpu->AB));
        break;
    default:
        ep_verify(false);
    };

    #undef MOS_STORE_REG
    #undef MOS_REG
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
    cpu->S = 0xfd;
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

bool mos6502_tick(struct mos6502_cpu* cpu)
{
    struct mos6502_instr next_instr = {0};

    ep_verify(cpu != NULL);
    ep_assert(cpu->instr.cycle < cpu->instr.ncycles);

    if (cpu->halted) {
        return false;
    }

    /* The last cycle of every instr is to fetch the next opcode, unless it's halting */
    ep_verify(cpu->instr.ncycles > 0);
    if (cpu->instr.uop != MOS_UOP_HLT && cpu->instr.cycle == cpu->instr.ncycles - 1) {
        next_instr = fetch_next_instr(cpu);
    } else if (cpu->instr.address_latched || addr_mode_exec(cpu)) {
        ep_verify(cpu->instr.address_latched);
        uop_exec(cpu);
    }

    cpu->cycle++;
    cpu->instr.cycle++;

    /* Retire the current instruction */
    if (cpu->instr.cycle == cpu->instr.ncycles) {
        cpu->total_retired++;
        cpu->instr = next_instr;
        return true;
    }

    return false;
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
    ep_verify(!(cpu.P & SR_B));
    ep_verify(!(cpu.P & SR_D));
    ep_verify(cpu.P & SR_I);
    ep_verify(!cpu.halted);

    free_ram_region(ram);
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
