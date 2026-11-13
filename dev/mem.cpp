/**
 * dev/mem.cpp — /dev/mem: read-only, the RP2040's memory, by address
 *
 *   hexdump -s 0x10000000 -n 256 /dev/mem    start of the flash (boot2, then picoos)
 *   hexdump -s 0x20000000 -n 64 /dev/mem     start of RAM
 *   hexdump -n 64 /dev/mem                   the boot ROM
 *
 * The offset is the address. Only regions that are safe to read are
 * allowed; anything else is an error, never a crash:
 *
 *   0x00000000  boot ROM   16 KB
 *   0x10000000  flash      PICO_FLASH_SIZE_BYTES (2 MB, through XIP)
 *   0x20000000  RAM        264 KB (incl. the 2 x 4 KB scratch banks)
 *
 * Left out on purpose: unmapped addresses (reading one is a bus fault,
 * the chip crashes) and peripheral registers (reading some of them
 * changes them: a UART or USB FIFO loses its data). Read-only, like
 * /dev/fram: writing raw memory from a shell is a way to crash, not a
 * feature.
 */
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdint>

static const struct { uint32_t start, size; } regions[] = {
    { 0x00000000, 16 * 1024 },
    { 0x10000000, PICO_FLASH_SIZE_BYTES },
    { 0x20000000, 264 * 1024 },
};

static int mem_pread(char *buf, int len, uint32_t addr)
{
    for (const auto &r : regions) {
        if (addr == r.start + r.size) return 0;          // ran up to the end: EOF, not an error
        if (addr < r.start || addr - r.start >= r.size) continue;
        uint32_t left = r.size - (addr - r.start);
        if ((uint32_t)len > left) len = left;   // stop at the region's end
        // volatile byte reads: address 0 is real memory here (the boot
        // ROM), and must not be treated as a null pointer by the compiler.
        const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)addr;
        for (int i = 0; i < len; i++) buf[i] = p[i];
        return len;
    }
    return -1;
}

static int mem_read(char *buf, int len)
{
    return mem_pread(buf, len, 0);
}

static const device_t dev_mem = {"/dev/mem", 0, 0, mem_read, 0, mem_pread};

void mem_register(void)
{
    fs_register(&dev_mem);
}
