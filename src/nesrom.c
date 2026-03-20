/*
 * iNES/NES2.0 ROM formats
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "defs.h"
#include "2a0x.h"

struct nes_rom_header
{
    #define EP_NES_ROM_SIGNATURE 0x1a53454e
    uint32_t signature;
    uint8_t prgsize;
    uint8_t chrsize;
    union {
        uint16_t as_u16;
        struct {
            uint16_t vmirroring:1;
            uint16_t prgram:1;
            uint16_t trainer:1;
            uint16_t vram4:1;
            uint16_t mapper_low:4;
            uint16_t console:2;
            uint16_t nes20:2;
            uint16_t mapper_high:4;
        };
    } flags;

    /* TODO: these are nes2 extentions */
    uint8_t _reservedz[8];
};
ep_static_assert(sizeof(struct nes_rom_header) == 16);

struct nes_rom
{
    struct nes_rom_header header;

    void* imagebase;
    size_t imagesize;

    void* prgrom;
    size_t prgromsize;

    void* chrrom;
    size_t chrromsize;

    void* trainer;
    size_t trainersize;

    uint8_t mapper_id;
};

static void* validate_rom_segment(struct nes_rom* rom, size_t size, off_t offset)
{
    ep_assert(size > 0);

    if (offset >= rom->imagesize || size > rom->imagesize) {
        return NULL;
    }

    if (rom->imagesize - offset < size) {
        return NULL;
    }

    return (uint8_t*)rom->imagebase + offset;
}

static void unmap_rom_file(struct nes_rom* rom)
{
    ep_assert(rom);

    if (rom->imagebase != NULL) {
        munmap(rom->imagebase, rom->imagesize);
        memset(rom, 0, sizeof(*rom));
    }
}

static bool parse_rom_file(const char* path, struct nes_rom* rom)
{
    ep_assert(path);
    ep_assert(rom);

    memset(rom, 0, sizeof(*rom));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    size_t fsize = lseek(fd, 0, SEEK_END);
    ep_verify(fsize >= 0);
    ep_verify(lseek(fd, 0, SEEK_SET) == 0);

    if (fsize < sizeof(rom->header)) {
        goto bad_rom;
    }

    void* pdata = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    ep_verify(pdata != MAP_FAILED);

    rom->imagebase = pdata;
    rom->imagesize = fsize;

    memcpy(&rom->header, pdata, sizeof(rom->header));

    if (rom->header.signature != EP_NES_ROM_SIGNATURE) {
        goto bad_rom;
    }

    ep_trace("ROM size %zu:\n PRGROM size %hhu (16k banks)\n CHRROM size %hhu (8k banks)\n flags 0x%04hx\n",
             fsize, rom->header.prgsize, rom->header.chrsize, rom->header.flags.as_u16);

    off_t offset = sizeof(rom->header);

    if (rom->header.flags.trainer) {
        rom->trainer = validate_rom_segment(rom, rom->trainersize = 512, offset);
        if (!rom->trainer) {
            goto bad_rom;
        }

        offset += rom->trainersize;
    }

    if (rom->header.prgsize > 0) {
        rom->prgrom = validate_rom_segment(rom, rom->prgromsize = (size_t)rom->header.prgsize << 14, offset);
        if (!rom->prgrom) {
            goto bad_rom;
        }

        offset += rom->prgromsize;
    } else {
        /* PRG ROM should be there */
        goto bad_rom;
    }

    if (rom->header.chrsize > 0) {
        rom->chrrom = validate_rom_segment(rom, rom->chrromsize = (size_t)rom->header.chrsize << 13, offset);
        if (!rom->chrrom) {
            goto bad_rom;
        }

        offset += rom->chrromsize;
    }

    if (rom->header.flags.prgram) {
        ep_trace("PRGRAM is not supported");
        goto bad_rom;
        //rom->prgramsize = (size_t)(rom->header.prgramsize == 0 ? 1 : rom->header.prgramsize) << 13;
    }

    rom->mapper_id = rom->header.flags.mapper_low | (rom->header.flags.mapper_high << 4);

    close(fd);
    return true;

bad_rom:
    close(fd);
    unmap_rom_file(rom);
    return false;
}

typedef mos_pa_t paddr16_t;

enum nes_cpu_segment_type
{
    NES_CPU_SEGMENT_RAM,
    NES_CPU_SEGMENT_MMIO,
    NES_CPU_SEGMENT_EXT,
    NES_CPU_SEGMENT_PRGRAM,
    NES_CPU_SEGMENT_PRGROM_LOWER,
    NES_CPU_SEGMENT_PRGROM_UPPER,

    NES_CPU_SEGMENT_TOTAL
};

/* CPU region map is indexed by the most-significant PA nibble (segment index) */
static const enum nes_cpu_segment_type cpu_segment_map[16] =
{
    /* 0x0000 - 0x1FFF */
    NES_CPU_SEGMENT_RAM,
    NES_CPU_SEGMENT_RAM,
    /* 0x2000 - 0x3FFF */
    NES_CPU_SEGMENT_MMIO,
    NES_CPU_SEGMENT_MMIO,
    /* 0x4000 - 0x5FFF */
    NES_CPU_SEGMENT_EXT,
    NES_CPU_SEGMENT_EXT,
    /* 0x6000 - 0x7FFF */
    NES_CPU_SEGMENT_PRGRAM,
    NES_CPU_SEGMENT_PRGRAM,
    /* 0x8000 - 0xBFFF */
    NES_CPU_SEGMENT_PRGROM_LOWER,
    NES_CPU_SEGMENT_PRGROM_LOWER,
    NES_CPU_SEGMENT_PRGROM_LOWER,
    NES_CPU_SEGMENT_PRGROM_LOWER,
    /* 0xC000 - 0xFFFF */
    NES_CPU_SEGMENT_PRGROM_UPPER,
    NES_CPU_SEGMENT_PRGROM_UPPER,
    NES_CPU_SEGMENT_PRGROM_UPPER,
    NES_CPU_SEGMENT_PRGROM_UPPER,
};

struct nes_system
{
    struct nes_rom rom;
    void* phymap[16];

    struct mos6502_cpu* cpu;
} nes;

static inline paddr16_t paddr2seg(paddr16_t paddr)
{
    return paddr >> 12;
}

static inline paddr16_t paddr2offset(paddr16_t paddr)
{
    return paddr & 0x0FFF;
}

static void* nes_decode_paddr(paddr16_t paddr)
{
    paddr16_t seg = paddr2seg(paddr);
    paddr16_t offset = paddr2offset(paddr);
    void* vaddr = NULL;

    switch (cpu_segment_map[seg]) {

    /* Internal ram, always mapped */
    case NES_CPU_SEGMENT_RAM:
        ep_assert(nes.phymap[seg]);
        vaddr = nes.phymap[seg] + offset;
        break;

    /* TODO */
    case NES_CPU_SEGMENT_MMIO:
        ep_assert(false);
        break;

    /* TODO */
    case NES_CPU_SEGMENT_EXT:
        ep_assert(false);
        break;

    /* Catridge ram, optional */
    case NES_CPU_SEGMENT_PRGRAM:
        vaddr = nes.phymap[seg] ? nes.phymap[seg] + offset : NULL;
        break;

    /* Lower/Upper 16k of PRGROM, always mapped */
    case NES_CPU_SEGMENT_PRGROM_LOWER:
    case NES_CPU_SEGMENT_PRGROM_UPPER:
        ep_assert(nes.phymap[seg]);
        vaddr = nes.phymap[seg] + offset;
        break;

    default:
        ep_assert(false);
        break;
    };

    return vaddr;
}

static bool nes_init_system(const char* romfile)
{
    ep_assert(romfile);

    memset(&nes, 0, sizeof(nes));

    if (!parse_rom_file(romfile, &nes.rom)) {
        fprintf(stderr, "failed to parse rom file \'%s\'\n", romfile);
        return false;
    }

    /*
     * PRGROM mapping depends on its size:
     * - 16K ROM is mirrored between 8000–BFFF and C000–FFFF.
     * - 32K ROM is placed to 8000-FFFF
     * - >32K ROM depends on the mapper (TODO)
     */

    if (nes.rom.prgromsize == 16 * 1024) {
        ep_trace("Mapping NROM-128");
        for (unsigned seg = 0; seg < nes.rom.prgromsize / 0x1000; seg++) {
            nes.phymap[0x8 + seg] = nes.rom.prgrom + (seg * 0x1000);
            nes.phymap[0xC + seg] = nes.rom.prgrom + (seg * 0x1000);
        }
    } else if (nes.rom.prgromsize == 32 * 1024) {
        ep_trace("Mapping NROM-256");
        for (unsigned seg = 0; seg < nes.rom.prgromsize / 0x1000; seg++) {
            nes.phymap[0x8 + seg] = nes.rom.prgrom + (seg * 0x1000);
        }
    } else {
        /* TODO */
        fprintf(stderr, "Unsupported PRGROM size %zu\n", nes.rom.prgromsize);
        return false;
    }

    nes.cpu = mos6502_create();
    ep_verify(nes.cpu);

    mos6502_init(nes.cpu);

    return true;
}

static void nes_run(void)
{
    ep_trace("nes_run");

    mos6502_reset(nes.cpu);

    while (!mos6502_is_halted(nes.cpu)) {
        mos6502_tick(nes.cpu);
    }
}

#if !defined(EP_CONFIG_TEST)

mos_word_t* mos6502_decode_paddr(struct mos6502_cpu* cpu, mos_pa_t paddr)
{
    (void)cpu;
    return nes_decode_paddr(paddr);
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        printf("Usage: nesrom <rom file>\n");
        exit(EXIT_FAILURE);
    }

    char* path = argv[1];
    if (!nes_init_system(path)) {
        exit(EXIT_FAILURE);
    }

    nes_run();

    return 0;
}

#endif
