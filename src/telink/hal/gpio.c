#include "hal/gpio.h"
#include "hal/tasks.h"
#include "telink_gpio_pin.h"
#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

#include <stdint.h>
#include <string.h>
// hal_gpio_pin_t directly stores GPIO_PinTypeDef values

// telink_gpio_pin.h encodes pins without the SDK; fail the build on mismatch.
typedef char telink_gpio_pin_encoding_matches_sdk[
        (GPIO_GROUPA == 0x000 && GPIO_GROUPB == 0x100 && GPIO_GROUPC == 0x200 &&
         GPIO_GROUPD == 0x300 && GPIO_GROUPE == 0x400 &&
         GPIO_PC2 == 0x204 && GPIO_PB7 == 0x180 && GPIO_PE3 == 0x408) ? 1 : -1];

// GPIO config tracking for deep retention re-init
// After deep retention wake, all GPIO SFRs are lost and must be replayed.
// The TLSR8258 has 36 GPIOs (A0..D7, E0..E3); every recorded pin is one of
// the 32 pins of ports A-D (device config pins and the recovery rest set).
#define MAX_GPIO_CONFIGS    32


typedef struct {
    hal_gpio_pin_t  pin;
    uint8_t         is_input;
    hal_gpio_pull_t pull;
    uint8_t         value;
} gpio_config_t;
static gpio_config_t gpio_configs[MAX_GPIO_CONFIGS];
static uint8_t       gpio_config_cnt = 0;

static gpio_config_t *find_gpio_config(hal_gpio_pin_t pin) {
    for (uint8_t i = 0; i < gpio_config_cnt; i++) {
        if (gpio_configs[i].pin == pin) {
            return &gpio_configs[i];
        }
    }
    return NULL;
}

// Convert HAL pull type to Telink pull type
static GPIO_PullTypeDef hal_to_telink_pull(hal_gpio_pull_t pull) {
    switch (pull) {
    case HAL_GPIO_PULL_NONE:
        return PM_PIN_UP_DOWN_FLOAT;

    case HAL_GPIO_PULL_UP:
        return PM_PIN_PULLUP_10K;

    case HAL_GPIO_PULL_UP_1M:
        return PM_PIN_PULLUP_1M;

    case HAL_GPIO_PULL_DOWN:
        return PM_PIN_PULLDOWN_100K;

    default:
        return PM_PIN_UP_DOWN_FLOAT;
    }
}

static void gpio_init_hw(hal_gpio_pin_t gpio_pin, uint8_t is_input,
                         hal_gpio_pull_t pull) {
    GPIO_PinTypeDef telink_pin = (GPIO_PinTypeDef)gpio_pin;

    if (is_input) {
        gpio_set_input_en(telink_pin, 1);
        gpio_set_output_en(telink_pin, 0);
    } else {
        gpio_set_output_en(telink_pin, 1);
        gpio_set_input_en(telink_pin, 0);
    }

    // Set pull resistor
    gpio_setup_up_down_resistor(telink_pin, hal_to_telink_pull(pull));
}

void hal_gpio_init(hal_gpio_pin_t gpio_pin, uint8_t is_input,
                   hal_gpio_pull_t pull) {
    gpio_init_hw(gpio_pin, is_input, pull);

    gpio_config_t *config = find_gpio_config(gpio_pin);
    if (config == NULL) {
        if (gpio_config_cnt >= MAX_GPIO_CONFIGS) {
            return;
        }
        config = &gpio_configs[gpio_config_cnt++];
    }

    // Track config for deep retention re-init
    config->pin      = gpio_pin;
    config->is_input = is_input;
    config->value    = 0;
    config->pull     = pull;
}

/*
 * TLSR8258 general-purpose pins: ports A-D. Left out: PA7 (SWS, the only way
 * back to stock firmware), port E (PE0-PE3 are the internal flash SPI), PB1
 * when the debug UART transmits on it, PC2/PC3 when a 32 kHz crystal is
 * fitted. The 24 MHz crystal has dedicated pads.
 */
static const hal_gpio_pin_t rest_pins[] = {
    GPIO_PA0, GPIO_PA1, GPIO_PA2, GPIO_PA3, GPIO_PA4, GPIO_PA5, GPIO_PA6,
    GPIO_PB0,
#if !UART_PRINTF_MODE
    GPIO_PB1,
#endif
    GPIO_PB2, GPIO_PB3, GPIO_PB4, GPIO_PB5, GPIO_PB6, GPIO_PB7,
    GPIO_PC0, GPIO_PC1,
#if !defined(CLOCK_32K_EXT_CRYSTAL) || !CLOCK_32K_EXT_CRYSTAL
    GPIO_PC2, GPIO_PC3,
#endif
    GPIO_PC4, GPIO_PC5, GPIO_PC6, GPIO_PC7,
    GPIO_PD0, GPIO_PD1, GPIO_PD2, GPIO_PD3, GPIO_PD4, GPIO_PD5, GPIO_PD6,
    GPIO_PD7,
};

uint8_t telink_gpio_rest_pins(const hal_gpio_pin_t **pins) {
    *pins = rest_pins;
    return (uint8_t)(sizeof(rest_pins) / sizeof(rest_pins[0]));
}

/*
 * The output latch is cleared first: a PA or PD pad whose latch holds 1 keeps
 * reading 1 with its output disabled and the 100 kOhm pull-down applied.
 * hal_gpio_init() records the pin, so a deep-retention wake restores it.
 */
void hal_gpio_rest_all(void) {
    for (uint8_t i = 0; i < sizeof(rest_pins) / sizeof(rest_pins[0]); i++) {
        GPIO_PinTypeDef pin = (GPIO_PinTypeDef)rest_pins[i];

        if (gpio_is_output_en(pin)) {
            continue;
        }
        gpio_write(pin, 0);
        gpio_set_func(pin, AS_GPIO);
        hal_gpio_init(rest_pins[i], 1, HAL_GPIO_PULL_DOWN);
    }
}

void telink_gpio_reinit_after_deep_retention(void) {
    for (uint8_t i = 0; i < gpio_config_cnt; i++) {
        if (!gpio_configs[i].is_input) {
            // Restore output value before reconfiguring pin,
            // to avoid driving it to the 0 level unconditionally
            // briefly during re-init
            gpio_write((GPIO_PinTypeDef)gpio_configs[i].pin, gpio_configs[i].value);
        }
        gpio_init_hw(gpio_configs[i].pin, gpio_configs[i].is_input,
                     gpio_configs[i].pull);
    }
}

void telink_gpio_to_pull_for_deep_retention() {
    // Uses pull-up or downs to emulate output levels during deep retention,
    // as telink doesn't support true GPIO output retention.
    for (uint8_t i = 0; i < gpio_config_cnt; i++) {
        if (gpio_configs[i].is_input) {
            continue;
        }
        GPIO_PinTypeDef  telink_pin = (GPIO_PinTypeDef)gpio_configs[i].pin;
        GPIO_PullTypeDef pull       =
            gpio_configs[i].value ? PM_PIN_PULLUP_1M : PM_PIN_PULLDOWN_100K;
        gpio_set_input_en(telink_pin, 1);
        gpio_set_output_en(telink_pin, 0);
        gpio_setup_up_down_resistor(telink_pin, pull);
    }
}

void hal_gpio_set(hal_gpio_pin_t gpio_pin) {
    gpio_write((GPIO_PinTypeDef)gpio_pin, 1);
    gpio_config_t *config = find_gpio_config(gpio_pin);
    if (config) {
        config->value = 1;
    }
}

void hal_gpio_clear(hal_gpio_pin_t gpio_pin) {
    gpio_write((GPIO_PinTypeDef)gpio_pin, 0);
    gpio_config_t *config = find_gpio_config(gpio_pin);
    if (config) {
        config->value = 0;
    }
}

uint8_t hal_gpio_read(hal_gpio_pin_t gpio_pin) {
    return gpio_read((GPIO_PinTypeDef)gpio_pin) ? 1 : 0;
}

hal_gpio_pin_t hal_gpio_parse_pin(const char *s) {
    return telink_gpio_pin_from_text(s);
}

hal_gpio_pull_t hal_gpio_parse_pull(const char *pull_str) {
    if (!pull_str) {
        return HAL_GPIO_PULL_INVALID;
    }

    switch (pull_str[0]) {
    case 'u':
        return HAL_GPIO_PULL_UP;

    case 'U':
        return HAL_GPIO_PULL_UP_1M;

    case 'd':
    case 'D':
        return HAL_GPIO_PULL_DOWN;

    case 'f':
    case 'F':
    case 'n':
    case 'N':
        return HAL_GPIO_PULL_NONE;

    default:
        return HAL_GPIO_PULL_INVALID;
    }
}

// GPIO interrupt callbacks
