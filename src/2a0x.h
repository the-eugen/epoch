/*
 * Ricoh 2A03/2A07 CPU emulation.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  mos_word_t;
typedef uint16_t mos_pa_t;

/* MOS 6502 Micro-operations */
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
    MOS_UOP_BIT,
    MOS_UOP_CMP,
    MOS_UOP_CPX,
    MOS_UOP_CPY,
    MOS_UOP_JMP,
    MOS_UOP_JSR,
    MOS_UOP_RTS,
    MOS_UOP_BCC,
    MOS_UOP_BCS,
    MOS_UOP_BEQ,
    MOS_UOP_BMI,
    MOS_UOP_BNE,
    MOS_UOP_BPL,
    MOS_UOP_BVC,
    MOS_UOP_BVS,
};

/* MOS 6502 Addressing Modes */
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
    MOS_AM_IND,
    MOS_AM_INDX,
    MOS_AM_INDY,
    MOS_AM_REL,
};

struct mos6502_instr
{
    /* Human-friendly mnemonic */
    char mnemonic[4];

    /* Operation */
    uint8_t uop;

    /* Address mode */
    uint8_t mode;

    /* Instruction length in bytes including operand */
    uint8_t length;

    /* Total cycles this instruction takes to execute.
       Note that this is a default and might be adjusted when we run the uop. */
    uint8_t ncycles;

    /* Latch address to PC register instead of an internal address latch */
    #define MOS_CTRL_PC_ADDRESS_LATCH (1u << 0)
    /* This is a write instruction and will take an extra cycle */
    #define MOS_CTRL_RW (1u << 1)
    /* Insert a cross page delay cycle signal */
    #define MOS_CTRL_XPAGE_DELAY (1u << 2)
    /* Control logic bits */
    uint8_t ctrlbits;
};

struct mos6502_cpu;

/* Creates a new CPU instance */
struct mos6502_cpu* mos6502_create(void);

/* Initializes a CPU instance */
void mos6502_init(struct mos6502_cpu* cpu);

/* Resets the CPU state */
void mos6502_reset(struct mos6502_cpu* cpu);

/* Executes one CPU clock tick */
void mos6502_tick(struct mos6502_cpu* cpu);

/* Checks if CPU is halted */
bool mos6502_is_halted(struct mos6502_cpu* cpu);

/* Returns total number of retired instructions */
uint64_t mos6502_total_retired(struct mos6502_cpu* cpu);

/* Returns total elapsed CPU cycles */
uint64_t mos6502_cycles(struct mos6502_cpu* cpu);

/* Retrieves instruction definition for a given opcode */
const struct mos6502_instr* mos6502_get_instr(uint8_t opcode);

/*
 * Implemented by the system-level emulator when linking with us.
 * Decoded accesses can be read or write but are always word-sized.
 * Return NULL for unmapped access and expect the most recent tick to fail.
 */
extern mos_word_t* mos6502_decode_paddr(struct mos6502_cpu* cpu, mos_pa_t paddr);
