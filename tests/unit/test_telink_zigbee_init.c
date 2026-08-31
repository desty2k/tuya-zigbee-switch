#include "hal/zigbee.h"
#include "support/runner.h"
#include "zcl_include.h"
#include <stdint.h>

static char    init_trace[5];
static uint8_t init_trace_count;
static af_simple_descriptor_t descriptor;
static hal_zigbee_endpoint *  zcl_endpoints;
static uint8_t zcl_endpoints_count;
static af_simple_descriptor_t *ota_descriptors;
static af_simple_descriptor_t *bdb_descriptors;

void telink_zigbee_hal_network_init(void) {
    init_trace[init_trace_count++] = 'N';
}

void telink_zigbee_hal_zcl_init(hal_zigbee_endpoint *endpoints,
                                uint8_t endpoints_cnt) {
    init_trace[init_trace_count++] = 'Z';
    zcl_endpoints       = endpoints;
    zcl_endpoints_count = endpoints_cnt;
}

af_simple_descriptor_t *telink_zigbee_hal_zcl_get_descriptors(void) {
    return &descriptor;
}

void hal_zigbee_init_ota(void) {
    init_trace[init_trace_count++] = 'O';
    ota_descriptors = telink_zigbee_hal_zcl_get_descriptors();
}

void telink_zigbee_hal_bdb_init(af_simple_descriptor_t *endpoint_descriptor) {
    init_trace[init_trace_count++] = 'B';
    bdb_descriptors = endpoint_descriptor;
}

TEST(initializes_ota_after_zcl_and_before_bdb) {
    hal_zigbee_endpoint endpoints[2] = { 0 };

    hal_zigbee_init(endpoints, 2);

    ASSERT_EQ(4, init_trace_count);
    ASSERT_EVENT_LOG_EQ("NZOB", init_trace, init_trace_count);
    ASSERT_TRUE(zcl_endpoints == endpoints);
    ASSERT_EQ(2, zcl_endpoints_count);
    ASSERT_TRUE(ota_descriptors == &descriptor);
    ASSERT_TRUE(bdb_descriptors == &descriptor);
}

int main(void) {
    RUN_TEST(initializes_ota_after_zcl_and_before_bdb);
    return test_runner_failures != 0;
}
