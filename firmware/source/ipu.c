#include "api.h"
#include "pico/stdlib.h"

#define MAX_BUTTONS 8

static const uint8_t BUTTON_PINS[MAX_BUTTONS] = {29, 28, 27, 26, 15, 14, 8, 0};

static struct {
        uint8_t state;
} ipu;

void ipu_init(void) {
    ipu.state = 0;

    for (int button = 0; button < MAX_BUTTONS; button++) {
        gpio_init(BUTTON_PINS[button]);
        gpio_set_dir(BUTTON_PINS[button], GPIO_IN);
        gpio_pull_down(BUTTON_PINS[button]);
    }
}

uint8_t ipu_read(void) {
    ipu.state = 0;

    for (int button = 0; button < MAX_BUTTONS; button++) {
        ipu.state |= gpio_get(BUTTON_PINS[button]) << button;
    }

    return ipu.state;
}

uint8_t ipu_get_state(void) {
    return ipu.state;
}