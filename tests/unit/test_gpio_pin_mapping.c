#include "hal/gpio_pin_text.h"
#include "silabs/hal/silabs_gpio_pin.h"
#include "support/runner.h"
#include "telink/hal/telink_gpio_pin.h"

TEST(pin_text_accepts_only_a_whole_upper_case_name) {
    uint8_t port  = 0xFF;
    uint8_t index = 0xFF;

    ASSERT_TRUE(hal_gpio_pin_text_split("C2", &port, &index));
    ASSERT_EQ(2, port);
    ASSERT_EQ(2, index);
    ASSERT_TRUE(hal_gpio_pin_text_split("Z15", &port, &index));
    ASSERT_EQ(25, port);
    ASSERT_EQ(15, index);
    ASSERT_TRUE(!hal_gpio_pin_text_split(NULL, &port, &index));
    ASSERT_TRUE(!hal_gpio_pin_text_split("", &port, &index));
    ASSERT_TRUE(!hal_gpio_pin_text_split("C", &port, &index));
    ASSERT_TRUE(!hal_gpio_pin_text_split("c2", &port, &index));
    ASSERT_TRUE(!hal_gpio_pin_text_split("C2u", &port, &index));
    ASSERT_TRUE(!hal_gpio_pin_text_split("C16", &port, &index));
    ASSERT_TRUE(!hal_gpio_pin_text_split("C002", &port, &index));
    ASSERT_TRUE(!hal_gpio_pin_text_split("2C", &port, &index));
}

TEST(telink_pins_are_sdk_gpio_pin_values) {
    // GPIO_PinTypeDef: GPIO_GROUPx | BIT(n).
    ASSERT_EQ(0x001, telink_gpio_pin_from_text("A0"));
    ASSERT_EQ(0x204, telink_gpio_pin_from_text("C2"));
    ASSERT_EQ(0x180, telink_gpio_pin_from_text("B7"));
    ASSERT_EQ(0x308, telink_gpio_pin_from_text("D3"));
    ASSERT_EQ(0x480, telink_gpio_pin_from_text("E7"));
}

TEST(telink_rejects_pins_the_chip_lacks) {
    ASSERT_EQ(HAL_INVALID_PIN, telink_gpio_pin_from_text("A8"));
    ASSERT_EQ(HAL_INVALID_PIN, telink_gpio_pin_from_text("C15"));
    ASSERT_EQ(HAL_INVALID_PIN, telink_gpio_pin_from_text("F0"));
    ASSERT_EQ(HAL_INVALID_PIN, telink_gpio_pin_from_text("C2u"));
    ASSERT_EQ(HAL_INVALID_PIN, telink_gpio_pin_from_text(NULL));
}

TEST(telink_pin_index_is_the_bit_position) {
    ASSERT_EQ(0, telink_gpio_pin_index(telink_gpio_pin_from_text("A0")));
    ASSERT_EQ(2, telink_gpio_pin_index(telink_gpio_pin_from_text("C2")));
    ASSERT_EQ(7, telink_gpio_pin_index(telink_gpio_pin_from_text("B7")));
}

TEST(silabs_pins_pack_emlib_port_and_pin_number) {
    // gpioPortA..D = 0..3 in the upper byte, pin number in the lower byte.
    ASSERT_EQ(0x000, silabs_gpio_pin_from_text("A0"));
    ASSERT_EQ(0x202, silabs_gpio_pin_from_text("C2"));
    ASSERT_EQ(0x303, silabs_gpio_pin_from_text("D3"));
    ASSERT_EQ(0x008, silabs_gpio_pin_from_text("A8"));
    ASSERT_EQ(0x10F, silabs_gpio_pin_from_text("B15"));
}

TEST(silabs_rejects_pins_the_chip_lacks) {
    ASSERT_EQ(HAL_INVALID_PIN, silabs_gpio_pin_from_text("E0"));
    ASSERT_EQ(HAL_INVALID_PIN, silabs_gpio_pin_from_text("D16"));
    ASSERT_EQ(HAL_INVALID_PIN, silabs_gpio_pin_from_text("C2d"));
    ASSERT_EQ(HAL_INVALID_PIN, silabs_gpio_pin_from_text(NULL));
}

int main(void) {
    RUN_TEST(pin_text_accepts_only_a_whole_upper_case_name);
    RUN_TEST(telink_pins_are_sdk_gpio_pin_values);
    RUN_TEST(telink_rejects_pins_the_chip_lacks);
    RUN_TEST(telink_pin_index_is_the_bit_position);
    RUN_TEST(silabs_pins_pack_emlib_port_and_pin_number);
    RUN_TEST(silabs_rejects_pins_the_chip_lacks);

    return test_runner_failures == 0 ? 0 : 1;
}
