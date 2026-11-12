#include "board.h"
#include "hardware/gpio.h"

void board_i2c_init(void)
{
    i2c_init(BOARD_I2C, BOARD_I2C_HZ);
    gpio_set_function(BOARD_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_I2C_SCL, GPIO_FUNC_I2C);
    // External 4k7 pull-ups on the board, no internal ones needed.
}

void board_led_init(void)
{
    gpio_init(PIN_ALARM_LED);
    gpio_set_dir(PIN_ALARM_LED, GPIO_OUT);
    gpio_set_drive_strength(PIN_ALARM_LED, GPIO_DRIVE_STRENGTH_8MA); // ~7mA, see board.h
    gpio_put(PIN_ALARM_LED, 1); // off (inverted)
}

void board_lamp_init(void)
{
    gpio_init(PIN_LAMP);
    gpio_set_dir(PIN_LAMP, GPIO_OUT);
    gpio_set_drive_strength(PIN_LAMP, GPIO_DRIVE_STRENGTH_8MA); // assumes an LED + 220R like GPIO1's
    gpio_put(PIN_LAMP, !PIN_LAMP_ON); // off
}
