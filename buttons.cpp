/**
 * buttons.cpp — button debounce, from picoos's buttons.cpp
 *
 * Same repeating_timer_callback() logic, minus the unused start-button
 * IRQ and timer leftovers picoos still carried from the original warmer.
 */
#include "buttons.h"
#include "board.h"
#include "pico/stdlib.h"

#define DEBOUNCE_SHORT 5 // timer ticks (5ms each) ignored after an edge

static const uint button_pin[BUTTON_COUNT] = { BUTTON_PIN_UP,
                                               BUTTON_PIN_DOWN,
                                               BUTTON_PIN_MUTE,
                                               BUTTON_PIN_MANUAL,
                                               BUTTON_PIN_START,
                                               BUTTON_PIN_LAMP };

volatile bool button[BUTTON_COUNT];
static volatile bool button_pressed[BUTTON_COUNT];
static volatile int  button_cnt[BUTTON_COUNT];

static bool repeating_timer_callback(struct repeating_timer *t)
{
    (void)t;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if ((gpio_get(button_pin[i]) == false) && button_pressed[i]==false && button_cnt[i]==0) {
            button_pressed[i]=true; button[i]=true; button_cnt[i]=DEBOUNCE_SHORT;
        } else if((gpio_get(button_pin[i]) == true) && button_pressed[i]==true && button_cnt[i]==0) {
            button_pressed[i]=false; button_cnt[i]=DEBOUNCE_SHORT;
        } else if(button_cnt[i]>0) button_cnt[i]--;
    }
    return true;
}

static struct repeating_timer timer;

void buttons_init(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_init(button_pin[i]);
        gpio_pull_up(button_pin[i]);
    }
    add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &timer);
}
