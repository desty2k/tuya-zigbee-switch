#include "hal/zigbee.h"
#include "support/runner.h"
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

int main(void) {
    RUN_TEST(exact_capacity_appends_all_clusters);
    RUN_TEST(excess_reservation_preserves_destination);
    RUN_TEST(null_storage_is_rejected);
    RUN_TEST(graph_validation_rejects_invalid_storage_before_copy);
    RUN_TEST(exact_lookup_keeps_role_and_manufacturer_isolated);
    RUN_TEST(graph_validation_rejects_duplicate_exact_cluster_tuple);
    return test_runner_failures != 0;
}
