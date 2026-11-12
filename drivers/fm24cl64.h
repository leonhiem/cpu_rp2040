/**
 * drivers/fm24cl64.h — FM24CL64B 8 KB FRAM on the board's I2C bus
 *
 * 2-byte memory address, no page size, no write delay: a write is done
 * when the I2C transfer is. Any length, any address up to FRAM_SIZE
 * (board.h); longer writes go out in chunks.
 *
 * Shared by bringup (cell/aliasing tests) and picoos.
 */
#pragma once

#include <stdint.h>

bool fram_read(uint16_t addr, uint8_t *buf, int len);
bool fram_write(uint16_t addr, const uint8_t *buf, int len);
