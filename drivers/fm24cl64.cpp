#include "fm24cl64.h"
#include "board.h"
#include <cstring>

#define I2C_TIMEOUT_US 10000
#define WRITE_CHUNK    64

bool fram_read(uint16_t addr, uint8_t *buf, int len)
{
    if (len <= 0 || addr + len > FRAM_SIZE)
        return false;
    uint8_t a[2] = { (uint8_t)(addr >> 8), (uint8_t)addr };
    if (i2c_write_timeout_us(BOARD_I2C, FRAM_I2C_ADDR, a, 2, true, I2C_TIMEOUT_US) != 2)
        return false;
    return i2c_read_timeout_us(BOARD_I2C, FRAM_I2C_ADDR, buf, len, false,
                               I2C_TIMEOUT_US + len * 100) == len;
}

bool fram_write(uint16_t addr, const uint8_t *buf, int len)
{
    if (len <= 0 || addr + len > FRAM_SIZE)
        return false;
    uint8_t tmp[2 + WRITE_CHUNK];
    while (len > 0) {
        int n = len < WRITE_CHUNK ? len : WRITE_CHUNK;
        tmp[0] = addr >> 8;
        tmp[1] = addr;
        memcpy(tmp + 2, buf, n);
        if (i2c_write_timeout_us(BOARD_I2C, FRAM_I2C_ADDR, tmp, n + 2, false,
                                 I2C_TIMEOUT_US + n * 100) != n + 2)
            return false;
        addr += n;
        buf += n;
        len -= n;
    }
    return true;
}
