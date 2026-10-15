#include "base_components/fixed_math.h"
#include "support/runner.h"
#include "zigbee/meter_units.h"

TEST(products_and_quotients_match_native_64_bit_math) {
    const uint32_t values[] = { 0, 1, 2, 999, 1000000, 65535, 65536, 0x89ABCDEFu, UINT32_MAX };

    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        for (unsigned j = 0; j < sizeof(values) / sizeof(values[0]); j++) {
            uint64_t product = (uint64_t)values[i] * values[j];
            uint32_t remainder;

            ASSERT_TRUE(fixed_mul_u32(values[i], values[j]) == product);
            if (values[j] != 0) {
                ASSERT_TRUE(fixed_div_u64(product + values[i], values[j], &remainder) ==
                            (product + values[i]) / values[j]);
                ASSERT_TRUE(remainder == (product + values[i]) % values[j]);
            }
        }
    }
    ASSERT_EQ(UINT32_MAX, fixed_muldiv_u32(UINT32_MAX, 2, 1));
    ASSERT_EQ(1500000, fixed_muldiv_u32(1000000, 1500000, 1000000));
}

TEST(wire_units_round_and_saturate_below_the_invalid_value) {
    ASSERT_EQ(2370, meter_units_voltage_dv(236950));
    ASSERT_EQ(2369, meter_units_voltage_dv(236949));
    ASSERT_EQ(0xFFFE, meter_units_voltage_dv(UINT32_MAX));
    ASSERT_EQ(0xFFFE, meter_units_current_ma(70000));
    ASSERT_EQ(7, (uint16_t)meter_units_power_w(6806));
    ASSERT_EQ(-7, meter_units_power_w(-6806));
    ASSERT_EQ(0, meter_units_power_w(499));
    ASSERT_EQ(32767, meter_units_power_w(INT32_MAX));
    ASSERT_EQ(-32767, meter_units_power_w(INT32_MIN));
    ASSERT_TRUE(meter_units_energy_wh(1999999) == 1);
    ASSERT_TRUE(meter_units_energy_wh(UINT64_MAX) == UINT64_MAX / 1000000u);
}

int main(void) {
    RUN_TEST(products_and_quotients_match_native_64_bit_math);
    RUN_TEST(wire_units_round_and_saturate_below_the_invalid_value);
    return test_runner_failures == 0 ? 0 : 1;
}
