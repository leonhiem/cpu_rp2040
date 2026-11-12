/**
 * dev/cputemp.cpp — /dev/cputemp: read-only, RP2040 die temperature, °C
 *
 * The chip's built-in sensor on ADC input 4, using the RP2040
 * datasheet's formula: T = 27 - (V - 0.706) / 0.001721. Rough (a few
 * degrees), but every board has one, so it's a free sensor to pipe into
 * thresh/hyst while trying things out.
 *
 * Each read averages CPUTEMP_SAMPLES conversions (~2 us each). One ADC
 * count is ~0.47 C, and single samples jumped by 1-3 counts between
 * reads (19.2, 20.6, 19.6, ...). The averaging lives inside read(), so
 * the file just gives a steadier number -- nothing to configure, no
 * ioctl(). Anything else (say, raw counts) would be another file.
 */
#include "kernel/fs.h"
#include "hardware/adc.h"
#include <cstdio>

#define CPUTEMP_SAMPLES 16

static int cputemp_read(char *buf, int len)
{
    adc_select_input(4);
    uint32_t sum = 0;
    for (int i = 0; i < CPUTEMP_SAMPLES; i++)
        sum += adc_read();
    float v = (float)sum / CPUTEMP_SAMPLES * 3.3f / 4096.0f;
    float t = 27.0f - (v - 0.706f) / 0.001721f;
    return snprintf(buf, len, "%.1f\n", t);
}

static const device_t dev_cputemp = {"/dev/cputemp", 0, 0, cputemp_read, 0};

void cputemp_register(void)
{
    adc_init();
    adc_set_temp_sensor_enabled(true);
    fs_register(&dev_cputemp);
}
