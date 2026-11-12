/**
 * buttons.h — debounced push buttons (same six as babywarmer/picoos)
 *
 * A 5ms repeating timer samples every button; a press sets that
 * button's sticky button[] flag, which stays set until a consumer
 * (dev/buttons.cpp's devices, prog/adjust.cpp) reads and clears it.
 */
#pragma once

#define BUTTON_UP     0
#define BUTTON_DOWN   1
#define BUTTON_MUTE   2
#define BUTTON_MANUAL 3
#define BUTTON_START  4
#define BUTTON_LAMP   5
#define BUTTON_COUNT  6

extern volatile bool button[BUTTON_COUNT];

// GPIO setup + start the debounce timer.
void buttons_init(void);
