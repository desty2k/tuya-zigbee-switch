#include "base_components/energy_measurement/bl0937.h"
#include "hal/gpio_counter.h"
#include "stub/hal/gpio_counter.h"
#include "support/fake_clock.h"
#include "support/fake_gpio.h"
#include "support/runner.h"

#define CF     4u
#define CF1    5u
#define SEL    6u

static const bl0937_config_t config = {
    .cf_pin                   = CF,
    .cf1_pin                  = CF1,
    .sel_pin                  = SEL,
    .sel_high_selects_voltage = true,
    .voltage_uv_per_hz        = 120670,
    .current_ua_per_hz        = 9050,
    .power_uw_per_hz          = 1063500,
};

static void reset(void) {
    fake_clock_reset();
    fake_gpio_reset();
    stub_gpio_counter_reset();
}

/* Rates of a load: CF in mHz, CF1 in mHz with SEL at voltage and current. */
static void load(uint32_t cf_mhz, uint32_t voltage_mhz, uint32_t current_mhz) {
    stub_gpio_counter_set_rate(CF, cf_mhz, HAL_INVALID_PIN, 0);
    stub_gpio_counter_set_rate(CF1, voltage_mhz, SEL, 1);
    stub_gpio_counter_set_rate(CF1, current_mhz, SEL, 0);
}

static meter_sample_t run(bl0937_t *dev, uint32_t seconds, uint64_t *energy_uwh) {
    meter_driver_t driver = bl0937_as_meter(dev);
    meter_sample_t sample = { 0 };

    for (uint32_t i = 0; i < seconds; i++) {
        fake_clock_advance_ms(1000);
        energy_meter_sample(&driver, &sample);
        if (energy_uwh != NULL) {
            *energy_uwh += sample.energy_delta_uwh;
        }
    }
    return sample;
}

TEST(converts_the_measured_outlet_rates) {
    bl0937_t       dev;
    meter_sample_t sample;
    uint64_t       energy = 0;

    reset();
    ASSERT_TRUE(bl0937_init(&dev, &config));
    ASSERT_EQ(1, hal_gpio_read(SEL));
    load(6400, 1964000, 3500);
    sample = run(&dev, 1, &energy);
    /* Nothing is valid before its first window closes. */
    ASSERT_EQ(METER_SAMPLE_ENERGY, sample.valid_mask);
    sample = run(&dev, 29, &energy);
    ASSERT_EQ(METER_SAMPLE_ENERGY | METER_SAMPLE_VOLTAGE | METER_SAMPLE_CURRENT |
              METER_SAMPLE_ACTIVE_POWER, sample.valid_mask);
    /* 1964 Hz -> 237.0 V; 3.5 Hz -> 31.7 mA; 6.4 Hz -> 6.806 W. */
    ASSERT_TRUE(sample.voltage_mv >= 236900 && sample.voltage_mv <= 237100);
    ASSERT_TRUE(sample.current_ma >= 30 && sample.current_ma <= 33);
    ASSERT_TRUE(sample.active_power_mw >= 6700 && sample.active_power_mw <= 6900);
    /* 30 s at 6.806 W = 56.7 mWh, exact to the pulse. */
    ASSERT_TRUE(energy >= 56500 && energy <= 56800);
    ASSERT_EQ(0, dev.error_count);
}

TEST(high_load_updates_every_cycle_and_selects_current_low) {
    bl0937_t       dev;
    meter_sample_t sample;

    reset();
    ASSERT_TRUE(bl0937_init(&dev, &config));
    /* 3680 W at 230 V, 16 A. */
    load(3460000, 1906000, 1768000);
    run(&dev, 2, NULL);
    ASSERT_EQ(0, hal_gpio_read(SEL));
    sample = run(&dev, 2, NULL);
    ASSERT_EQ(1, hal_gpio_read(SEL));
    ASSERT_TRUE(sample.voltage_mv >= 229900 && sample.voltage_mv <= 230100);
    ASSERT_TRUE(sample.current_ma >= 15990 && sample.current_ma <= 16010);
    ASSERT_TRUE(sample.active_power_mw >= 3679000 && sample.active_power_mw <= 3681000);
}

TEST(inverted_sel_and_zero_load) {
    bl0937_t        dev;
    bl0937_config_t inverted = config;
    meter_sample_t  sample;
    uint64_t        energy = 0;

    reset();
    inverted.sel_high_selects_voltage = false;
    ASSERT_TRUE(bl0937_init(&dev, &inverted));
    ASSERT_EQ(0, hal_gpio_read(SEL));
    stub_gpio_counter_set_rate(CF1, 1964000, SEL, 0);
    sample = run(&dev, 30, &energy);
    ASSERT_TRUE(sample.voltage_mv >= 236900 && sample.voltage_mv <= 237100);
    ASSERT_EQ(0, sample.current_ma);
    ASSERT_EQ(0, sample.active_power_mw);
    ASSERT_EQ(0, energy);
}

TEST(rejects_implausible_counts_and_keeps_the_remainder) {
    bl0937_t       dev;
    meter_sample_t sample;
    uint64_t       energy = 0;

    reset();
    ASSERT_TRUE(bl0937_init(&dev, &config));
    load(30000000, 0, 0);
    sample = run(&dev, 1, &energy);
    ASSERT_EQ(0, energy);
    ASSERT_TRUE(dev.error_count > 0);
    ASSERT_TRUE(!(sample.valid_mask & METER_SAMPLE_ACTIVE_POWER));
    /* One pulse is 295.4 uWh; the fraction carries to the next pulse. */
    load(1000, 0, 0);
    energy = 0;
    run(&dev, 10, &energy);
    ASSERT_EQ(2954, energy);
    bl0937_close(&dev);
    ASSERT_TRUE(!energy_meter_sample(&(meter_driver_t){ .ops = bl0937_as_meter(&dev).ops,
                                                       .context = &dev }, &sample));
}

TEST(init_fails_without_counter_resources) {
    bl0937_t dev;
    bl0937_t other;

    reset();
    ASSERT_TRUE(bl0937_init(&dev, &config));
    ASSERT_TRUE(!bl0937_init(&other, &config));
    ASSERT_EQ(HAL_GPIO_COUNTER_INVALID_ID, other.cf_counter);
    ASSERT_EQ(HAL_GPIO_COUNTER_INVALID_ID, other.cf1_counter);
}

int main(void) {
    RUN_TEST(converts_the_measured_outlet_rates);
    RUN_TEST(high_load_updates_every_cycle_and_selects_current_low);
    RUN_TEST(inverted_sel_and_zero_load);
    RUN_TEST(rejects_implausible_counts_and_keeps_the_remainder);
    RUN_TEST(init_fails_without_counter_resources);
    return test_runner_failures == 0 ? 0 : 1;
}
