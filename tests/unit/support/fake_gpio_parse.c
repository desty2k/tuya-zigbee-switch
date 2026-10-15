#include "fake_gpio_parse.h"
#include "hal/gpio_pin_text.h"

hal_gpio_pin_t hal_gpio_parse_pin(const char *s) {
    uint8_t port;
    uint8_t index;

    if (!hal_gpio_pin_text_split(s, &port, &index) || port == 'Z' - 'A') {
        return HAL_INVALID_PIN;
    }
    return FAKE_GPIO_PIN('A' + port, index);
}
