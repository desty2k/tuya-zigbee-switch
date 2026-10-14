#include "hal/zigbee.h"
#include "support/runner.h"
#include "zigbee/zcl_frame_check.h"
#include <string.h>

TEST(exact_capacity_appends_all_clusters) {
    hal_zigbee_cluster storage[2];
    hal_zigbee_cluster first = { .cluster_id = 1 };
    hal_zigbee_cluster second = { .cluster_id = 2 };
    hal_zigbee_endpoint endpoint = {
        .cluster_capacity = 2,
        .clusters         = storage,
    };

    ASSERT_TRUE(hal_zigbee_endpoint_reserve_clusters(
        &endpoint, endpoint.cluster_capacity, 2));
    ASSERT_TRUE(hal_zigbee_endpoint_add_cluster(
        &endpoint, endpoint.cluster_capacity, &first));
    ASSERT_TRUE(hal_zigbee_endpoint_add_cluster(
        &endpoint, endpoint.cluster_capacity, &second));
    ASSERT_EQ(2, endpoint.cluster_count);
    ASSERT_EQ(1, storage[0].cluster_id);
    ASSERT_EQ(2, storage[1].cluster_id);
}

TEST(excess_reservation_preserves_destination) {
    hal_zigbee_cluster storage[2];
    hal_zigbee_cluster cluster = { .cluster_id = 7 };
    hal_zigbee_endpoint endpoint = {
        .cluster_count    = 1,
        .cluster_capacity = 2,
        .clusters         = storage,
    };

    memset(storage, 0xa5, sizeof(storage));
    ASSERT_TRUE(!hal_zigbee_endpoint_reserve_clusters(
        &endpoint, endpoint.cluster_capacity, 2));
    ASSERT_TRUE(!hal_zigbee_endpoint_add_cluster(
        &endpoint, endpoint.cluster_capacity + 1, &cluster));
    ASSERT_EQ(1, endpoint.cluster_count);
    ASSERT_EQ(0xa5a5, storage[1].cluster_id);
}

TEST(null_storage_is_rejected) {
    hal_zigbee_cluster cluster = { .cluster_id = 1 };
    hal_zigbee_endpoint endpoint = { 0 };

    ASSERT_TRUE(!hal_zigbee_endpoint_reserve_clusters(&endpoint, 1, 1));
    ASSERT_TRUE(!hal_zigbee_endpoint_add_cluster(&endpoint, 1, &cluster));
}

TEST(graph_validation_rejects_invalid_storage_before_copy) {
    hal_zigbee_cluster cluster_storage[1] = { { 0 } };
    hal_zigbee_endpoint endpoint = {
        .endpoint         = 1,
        .cluster_capacity = 1,
        .clusters         = cluster_storage,
    };

    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_OK,
              hal_zigbee_validate_descriptor_graph(&endpoint, 1, 1, 1, 1, 0));
    endpoint.cluster_count = 2;
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_CLUSTER_CAPACITY,
              hal_zigbee_validate_descriptor_graph(&endpoint, 1, 1, 1, 1, 0));
    endpoint.cluster_count = 0;
    endpoint.endpoint = 0;
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_ID,
              hal_zigbee_validate_descriptor_graph(&endpoint, 1, 1, 1, 1, 0));
}

TEST(exact_lookup_keeps_role_and_manufacturer_isolated) {
    uint8_t standard_value = 1;
    uint8_t private_value = 2;
    hal_zigbee_attribute standard_attr = { .attribute_id = 3, .value = &standard_value };
    hal_zigbee_attribute private_attr = { .attribute_id = 3, .value = &private_value };
    hal_zigbee_cluster clusters[] = {
        { .cluster_id = 6, .role = HAL_ZIGBEE_ROLE_SERVER, .attributes = &standard_attr,
          .attribute_count = 1 },
        { .cluster_id = 6, .manufacturer_code = 0x1A2B,
          .role = HAL_ZIGBEE_ROLE_SERVER, .attributes = &private_attr,
          .attribute_count = 1 },
        { .cluster_id = 6, .role = HAL_ZIGBEE_ROLE_CLIENT },
    };
    hal_zigbee_endpoint endpoint = {
        .endpoint = 1, .cluster_count = 3, .cluster_capacity = 3, .clusters = clusters,
    };

    ASSERT_TRUE(hal_zigbee_find_attribute_exact(
        &endpoint, 1, 1, 6, HAL_ZIGBEE_ROLE_SERVER, 0, 3) == &standard_attr);
    ASSERT_TRUE(hal_zigbee_find_attribute_exact(
        &endpoint, 1, 1, 6, HAL_ZIGBEE_ROLE_SERVER, 0x1A2B, 3) == &private_attr);
    ASSERT_TRUE(hal_zigbee_find_attribute_exact(
        &endpoint, 1, 1, 6, HAL_ZIGBEE_ROLE_CLIENT, 0, 3) == NULL);
}

TEST(graph_validation_rejects_duplicate_exact_cluster_tuple) {
    hal_zigbee_cluster clusters[] = {
        { .cluster_id = 6, .role = HAL_ZIGBEE_ROLE_SERVER },
        { .cluster_id = 6, .role = HAL_ZIGBEE_ROLE_SERVER },
    };
    hal_zigbee_endpoint endpoint = {
        .endpoint = 1, .cluster_count = 2, .cluster_capacity = 2, .clusters = clusters,
    };

    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_UNREGISTERED_CLUSTER,
              hal_zigbee_validate_descriptor_graph(&endpoint, 1, 1, 1, 2, 0));
}

TEST(usage_counts_every_cluster_once_and_ota_once_per_device) {
    hal_zigbee_cluster first[] = {
        { .cluster_id = 0x0000, .role = HAL_ZIGBEE_ROLE_SERVER, .attribute_count = 2 },
        { .cluster_id = HAL_ZIGBEE_CLUSTER_ID_OTA, .role = HAL_ZIGBEE_ROLE_CLIENT,
          .attribute_count = 3 },
        { .cluster_id = 0x0006, .role = HAL_ZIGBEE_ROLE_CLIENT },
    };
    hal_zigbee_cluster second[] = {
        { .cluster_id = HAL_ZIGBEE_CLUSTER_ID_OTA, .role = HAL_ZIGBEE_ROLE_CLIENT },
        { .cluster_id = 0x0006, .role = HAL_ZIGBEE_ROLE_SERVER, .attribute_count = 1 },
    };
    hal_zigbee_endpoint endpoints[] = {
        { .endpoint = 1, .cluster_count = 3, .cluster_capacity = 3, .clusters = first },
        { .endpoint = 2, .cluster_count = 2, .cluster_capacity = 2, .clusters = second },
    };
    hal_zigbee_graph_usage_t usage = hal_zigbee_graph_usage(endpoints, 2);

    ASSERT_EQ(2, usage.endpoints);
    ASSERT_EQ(2, usage.server_clusters);
    ASSERT_EQ(3, usage.client_clusters);
    ASSERT_EQ(6, usage.attributes);
    /* Basic, OnOff client, OnOff server, and one SDK OTA registration. */
    ASSERT_EQ(4, usage.zcl_registrations);
}

TEST(platform_limits_reject_each_exceeded_capacity) {
    hal_zigbee_graph_usage_t usage = { .endpoints = HAL_ZIGBEE_MAX_ENDPOINTS };

    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_OK, hal_zigbee_check_graph_usage(&usage));
    usage.endpoints++;
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_COUNT, hal_zigbee_check_graph_usage(&usage));

    usage = (hal_zigbee_graph_usage_t){ .server_clusters = HAL_ZIGBEE_MAX_SERVER_CLUSTERS + 1 };
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_CLUSTER_CAPACITY, hal_zigbee_check_graph_usage(&usage));
    usage = (hal_zigbee_graph_usage_t){ .client_clusters = HAL_ZIGBEE_MAX_CLIENT_CLUSTERS + 1 };
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_CLUSTER_CAPACITY, hal_zigbee_check_graph_usage(&usage));
    usage = (hal_zigbee_graph_usage_t){
        .server_clusters = HAL_ZIGBEE_MAX_SERVER_CLUSTERS,
        .client_clusters = HAL_ZIGBEE_MAX_CLUSTERS - HAL_ZIGBEE_MAX_SERVER_CLUSTERS + 1,
    };
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_CLUSTER_CAPACITY, hal_zigbee_check_graph_usage(&usage));
    usage = (hal_zigbee_graph_usage_t){ .attributes = HAL_ZIGBEE_MAX_ATTRIBUTES + 1 };
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_ATTRIBUTE_CAPACITY, hal_zigbee_check_graph_usage(&usage));
    usage = (hal_zigbee_graph_usage_t){
        .zcl_registrations = HAL_ZIGBEE_MAX_ZCL_REGISTRATIONS + 1,
    };
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_REGISTRATION_CAPACITY,
              hal_zigbee_check_graph_usage(&usage));
}

TEST(platform_graph_rejects_one_cluster_id_in_two_roles_on_an_endpoint) {
    hal_zigbee_cluster clusters[] = {
        { .cluster_id = 6, .role = HAL_ZIGBEE_ROLE_SERVER },
        { .cluster_id = 6, .role = HAL_ZIGBEE_ROLE_CLIENT },
    };
    hal_zigbee_endpoint endpoint = {
        .endpoint = 1, .cluster_count = 2, .cluster_capacity = 2, .clusters = clusters,
    };

    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_OK,
              hal_zigbee_validate_descriptor_graph(&endpoint, 1, 1, 1, 2, 0));
    ASSERT_EQ(HAL_ZIGBEE_DESCRIPTOR_DUPLICATE_CLUSTER_ID,
              hal_zigbee_validate_platform_graph(&endpoint, 1));
}

TEST(encoded_size_follows_string_prefix_and_rejects_overlong_strings) {
    const uint8_t char_str[]   = { 3, 'a', 'b', 'c' };
    const uint8_t long_str[]   = { 2, 0, 'a', 'b' };
    const uint8_t too_long[]   = { 9, 'a' };
    const uint8_t fixed_u16[2] = { 0x34, 0x12 };

    ASSERT_EQ(4, hal_zigbee_attr_encoded_size(HAL_ZIGBEE_TYPE_CHAR_STR, char_str, 33));
    ASSERT_EQ(4, hal_zigbee_attr_encoded_size(HAL_ZIGBEE_TYPE_LONG_CHAR_STR, long_str, 4));
    ASSERT_EQ(0, hal_zigbee_attr_encoded_size(HAL_ZIGBEE_TYPE_OCTET_STR, too_long, 2));
    ASSERT_EQ(2, hal_zigbee_attr_encoded_size(0x21, fixed_u16, 2));
}

#define FRAME_OK(cmd, bytes) \
        zcl_frame_is_well_formed((cmd), (bytes), sizeof(bytes), 16)

TEST(write_and_report_frames_must_end_after_their_last_record) {
    const uint8_t one_u8[]       = { 0x00, 0x40, 0x20, 0x07 };
    const uint8_t string[]       = { 0x02, 0x00, 0x44, 0x02, 0x00, 'M', ';' };
    const uint8_t wrapping[]     = { 0x02, 0x00, 0x43, 0xFD, 0xFF, 'x', 'y' };
    const uint8_t prefix_ffff[]  = { 0x02, 0x00, 0x44, 0xFF, 0xFF, 'x' };
    const uint8_t short_string[] = { 0x05, 0x00, 0x42, 0x09, 'a' };
    const uint8_t cut_prefix[]   = { 0x02, 0x00, 0x44, 0x02 };
    const uint8_t short_header[] = { 0x00, 0x40 };
    const uint8_t short_value[]  = { 0x00, 0x40, 0x21, 0x07 };
    const uint8_t trailing[]     = { 0x00, 0x40, 0x20, 0x07, 0x00 };
    const uint8_t structure[]    = { 0x00, 0x40, 0x4C, 0x00, 0x00 };

    ASSERT_TRUE(zcl_frame_is_well_formed(ZCL_FRAME_CMD_WRITE, NULL, 0, 16));
    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_WRITE, one_u8));
    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_WRITE_UNDIVIDED, string));
    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_REPORT, one_u8));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_WRITE, wrapping));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_WRITE_NO_RSP, wrapping));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_REPORT, wrapping));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_WRITE, prefix_ffff));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_WRITE, short_string));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_WRITE, cut_prefix));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_WRITE, short_header));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_WRITE, short_value));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_WRITE, trailing));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_REPORT, structure));
    /* Commands without length-carrying records pass unchanged. */
    ASSERT_TRUE(FRAME_OK(0x00, short_header));
}

TEST(read_response_values_are_bounded_only_for_successful_records) {
    const uint8_t failed[]   = { 0x00, 0x40, 0x86 };
    const uint8_t value[]    = { 0x00, 0x40, 0x00, 0x21, 0x34, 0x12 };
    const uint8_t wrapping[] = { 0x00, 0x40, 0x00, 0x43, 0xFD, 0xFF, 'x' };
    const uint8_t no_type[]  = { 0x00, 0x40, 0x00 };

    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_READ_RSP, failed));
    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_READ_RSP, value));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_READ_RSP, wrapping));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_READ_RSP, no_type));
}

TEST(configure_reporting_records_are_bounded_and_counted) {
    /* direction, attribute id, type, min, max, change (analog types only) */
    const uint8_t analog[]   = { 0x00, 0x55, 0x00, 0x21, 0x00, 0x00, 0x10, 0x0E, 0x01, 0x00 };
    const uint8_t discrete[] = { 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x0E };
    const uint8_t receive[]  = { 0x01, 0x00, 0x00, 0x10, 0x0E };
    const uint8_t cut[]      = { 0x00, 0x55, 0x00, 0x21, 0x00, 0x00, 0x10, 0x0E, 0x01 };
    uint8_t       many[5 * 17];

    for (uint8_t i = 0; i < 17; i++) {
        memcpy(&many[5 * i], receive, sizeof(receive));
    }
    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_CONFIG_REPORT, analog));
    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_CONFIG_REPORT, discrete));
    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_CONFIG_REPORT, receive));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_CONFIG_REPORT, cut));
    ASSERT_TRUE(zcl_frame_is_well_formed(ZCL_FRAME_CMD_CONFIG_REPORT, many, 5 * 16, 16));
    ASSERT_TRUE(!zcl_frame_is_well_formed(ZCL_FRAME_CMD_CONFIG_REPORT, many, 5 * 17, 16));
}

TEST(read_reporting_configuration_response_is_bounded) {
    const uint8_t failed[] = { 0x86, 0x00, 0x00, 0x00 };
    const uint8_t analog[] = { 0x00, 0x00, 0x55, 0x00, 0x21, 0x00, 0x00, 0x10, 0x0E, 0x01, 0x00 };
    const uint8_t cut[]    = { 0x00, 0x00, 0x55, 0x00, 0x21, 0x00, 0x00 };

    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_READ_REPORT_CFG_RSP, failed));
    ASSERT_TRUE(FRAME_OK(ZCL_FRAME_CMD_READ_REPORT_CFG_RSP, analog));
    ASSERT_TRUE(!FRAME_OK(ZCL_FRAME_CMD_READ_REPORT_CFG_RSP, cut));
}

int main(void) {
    RUN_TEST(write_and_report_frames_must_end_after_their_last_record);
    RUN_TEST(read_response_values_are_bounded_only_for_successful_records);
    RUN_TEST(configure_reporting_records_are_bounded_and_counted);
    RUN_TEST(read_reporting_configuration_response_is_bounded);
    RUN_TEST(usage_counts_every_cluster_once_and_ota_once_per_device);
    RUN_TEST(platform_limits_reject_each_exceeded_capacity);
    RUN_TEST(platform_graph_rejects_one_cluster_id_in_two_roles_on_an_endpoint);
    RUN_TEST(encoded_size_follows_string_prefix_and_rejects_overlong_strings);
    RUN_TEST(exact_capacity_appends_all_clusters);
    RUN_TEST(excess_reservation_preserves_destination);
    RUN_TEST(null_storage_is_rejected);
    RUN_TEST(graph_validation_rejects_invalid_storage_before_copy);
    RUN_TEST(exact_lookup_keeps_role_and_manufacturer_isolated);
    RUN_TEST(graph_validation_rejects_duplicate_exact_cluster_tuple);
    return test_runner_failures != 0;
}
