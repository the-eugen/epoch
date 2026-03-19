#pragma once

typedef uint8_t  mos_word_t;
typedef uint16_t mos_pa_t;

struct mos6502_cpu;

struct mos6502_cpu* mos6502_create(void);
void mos6502_init(struct mos6502_cpu* cpu);
void mos6502_reset(struct mos6502_cpu* cpu);

void mos6502_tick(struct mos6502_cpu* cpu);

bool mos6502_is_halted(struct mos6502_cpu* cpu);
uint64_t mos6502_total_retired(struct mos6502_cpu* cpu);
uint64_t mos6502_cycles(struct mos6502_cpu* cpu);

/*
 * Implemented by the system-level emulator when linking with us.
 * Decoded accesses can be read or write but are always word-sized.
 * Return NULL for unmapped access and expect the most recent tick to fail.
 */
extern mos_word_t* mos6502_decode_paddr(struct mos6502_cpu* cpu, mos_pa_t paddr);
