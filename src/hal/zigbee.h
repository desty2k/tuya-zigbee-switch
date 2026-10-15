#ifndef _HAL_ZIGBEE_H_
#define _HAL_ZIGBEE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal/zigbee_limits.h"

#define HAL_ZIGBEE_CMD_MAX_PAYLOAD        64
#define HAL_ZIGBEE_ENDPOINT_ID_MAX        10
#define HAL_ZIGBEE_CLUSTER_ID_OTA         0x0019u

/* ZCL string types; their encoded size comes from a length prefix. */
#define HAL_ZIGBEE_TYPE_OCTET_STR         0x41u
#define HAL_ZIGBEE_TYPE_CHAR_STR          0x42u
#define HAL_ZIGBEE_TYPE_LONG_OCTET_STR    0x43u
#define HAL_ZIGBEE_TYPE_LONG_CHAR_STR     0x44u

/** Zigbee attribute access permissions. Reporting is explicitly opt-in. */
typedef uint8_t hal_attr_flags_t;
#define HAL_ATTR_READ      (1u << 0)
#define HAL_ATTR_WRITE     (1u << 1)
#define HAL_ATTR_REPORT    (1u << 2)


/** Command handler result (processed or delegate to default handler) */
typedef enum {
    HAL_ZIGBEE_CMD_PROCESSED,
    HAL_ZIGBEE_CMD_SKIPPED,
    HAL_ZIGBEE_INVALID_VALUE,
    HAL_ZIGBEE_MALFORMED_COMMAND,
    HAL_ZIGBEE_ACTION_DENIED,
} hal_zigbee_cmd_result_t;

/** Zigbee network connection state */
typedef enum {
    HAL_ZIGBEE_NETWORK_NOT_JOINED = 0,
    HAL_ZIGBEE_NETWORK_JOINED     = 1,
    HAL_ZIGBEE_NETWORK_JOINING    = 2,
} hal_zigbee_network_status_t;

/** Zigbee cluster attribute definition (readable/writable device properties) */
typedef struct {
    uint16_t         attribute_id;
    uint8_t          data_type_id;
    hal_attr_flags_t flag;
    uint8_t          size;
    uint8_t *        value;
} hal_zigbee_attribute;

typedef enum {
    HAL_ZIGBEE_ROLE_SERVER,
    HAL_ZIGBEE_ROLE_CLIENT,
} hal_zigbee_cluster_role_t;

typedef enum {
    HAL_ZIGBEE_ATTR_VALIDATE,
    HAL_ZIGBEE_ATTR_COMMIT,
} hal_zigbee_attr_write_phase_t;

/** Zigbee command direction (client sends commands, server responds) */
typedef enum {
    HAL_ZIGBEE_DIR_CLIENT_TO_SERVER = 0,
    HAL_ZIGBEE_DIR_SERVER_TO_CLIENT = 1,
} hal_zigbee_direction_t;

/**
 * Write contract, identical on every platform, for one written attribute:
 *
 * 1. VALIDATE: the callback checks `candidate` and must not change any state.
 *    A result other than HAL_ZIGBEE_CMD_PROCESSED rejects the write.
 * 2. COMMIT: the callback applies `candidate`: it stores the value in the
 *    attribute storage (the cluster field), applies and persists it. It is
 *    the only place that applies and stores the value, and it may store a
 *    value other than `candidate` when the cluster derives the attribute
 *    from other state (the relay indicator state outside manual mode).
 *    A result other than HAL_ZIGBEE_CMD_PROCESSED rejects the write; the
 *    callback must then leave its state unchanged.
 * 3. The HAL never copies `candidate` over the storage of a cluster with a
 *    callback, so what COMMIT stored is what reads and reports return.
 *    Without a callback, the HAL copies `candidate` after its own checks.
 *
 * `candidate` holds the ZCL encoding (little-endian). `candidate_len` is its
 * encoded size: the type size for fixed-size types, length prefix plus data
 * for strings. The HAL rejects candidates longer than the attribute storage.
 *
 * Write Attributes handles its records one after another. Write Attributes
 * Undivided first validates every record and then writes each of them with
 * the steps above, so a rejected record leaves every attribute unchanged.
 * Known limitation: each record of Write Undivided is validated against the
 * current state, not against the state with the frame's earlier records
 * applied. A check that depends on another attribute of the same frame can
 * therefore pass in the first pass and fail at COMMIT in the second, after
 * earlier records were written.
 */
typedef hal_zigbee_cmd_result_t (*hal_zigbee_attr_write_callback_t)(
    uint8_t endpoint, uint16_t attribute_id, const uint8_t *candidate,
    uint16_t candidate_len, hal_zigbee_attr_write_phase_t phase);

static inline bool hal_zigbee_type_is_string(uint8_t data_type_id) {
    return data_type_id == HAL_ZIGBEE_TYPE_OCTET_STR ||
           data_type_id == HAL_ZIGBEE_TYPE_CHAR_STR ||
           data_type_id == HAL_ZIGBEE_TYPE_LONG_OCTET_STR ||
           data_type_id == HAL_ZIGBEE_TYPE_LONG_CHAR_STR;
}

/**
 * Encoded size of a ZCL value of `data_type_id` stored in `storage_size`
 * bytes. Returns 0 when a string's length prefix runs past `storage_size`,
 * and for the invalid-string prefixes 0xFF and 0xFFFF, which as a length
 * would wrap the SDK's 16-bit size (0xFFFF + 2 is 1).
 */
static inline uint16_t hal_zigbee_attr_encoded_size(uint8_t data_type_id,
                                                    const uint8_t *value,
                                                    uint16_t storage_size) {
    uint32_t size;

    if (data_type_id == HAL_ZIGBEE_TYPE_OCTET_STR ||
        data_type_id == HAL_ZIGBEE_TYPE_CHAR_STR) {
        if (storage_size < 1) return 0;

        if (value[0] == 0xFFu) return 0;

        size = 1u + value[0];
    } else if (data_type_id == HAL_ZIGBEE_TYPE_LONG_OCTET_STR ||
               data_type_id == HAL_ZIGBEE_TYPE_LONG_CHAR_STR) {
        if (storage_size < 2) return 0;

        size = (uint32_t)value[0] | ((uint32_t)value[1] << 8);
        if (size == 0xFFFFu) return 0;

        size += 2u;
    } else {
        size = storage_size;
    }
    return size <= storage_size ? (uint16_t)size : 0;
}

/** Immutable metadata and payload for one received ZCL command. */
typedef struct {
    uint8_t                source_endpoint;
    uint8_t                destination_endpoint;
    uint16_t               source_address;
    uint16_t               cluster_id;
    uint8_t                command_id;
    uint8_t                cluster_specific;
    uint8_t                manufacturer_specific;
    uint16_t               manufacturer_code;
    hal_zigbee_direction_t direction;
    const uint8_t *        payload;
    uint16_t               payload_len;
} hal_zigbee_incoming_command_t;

/** Function called when a declared cluster receives a command. */
typedef hal_zigbee_cmd_result_t (*hal_zigbee_cmd_callback_t)(
    const hal_zigbee_incoming_command_t *command);

/** Zigbee cluster (group of related attributes and commands) */
typedef struct {
    uint16_t                         cluster_id;
    uint16_t                         manufacturer_code;
    hal_zigbee_cluster_role_t        role;
    uint8_t                          attribute_count;
    hal_zigbee_attribute *           attributes;
    hal_zigbee_cmd_callback_t        cmd_callback;
    hal_zigbee_attr_write_callback_t attr_write_callback;
} hal_zigbee_cluster;

/** Zigbee endpoint (logical device with multiple clusters) */
typedef struct {
    uint8_t             endpoint;
    uint16_t            profile_id;
    uint16_t            device_id;
    uint8_t             device_version;

    uint8_t             cluster_count;
    uint8_t             cluster_capacity;
    hal_zigbee_cluster *clusters;
} hal_zigbee_endpoint;

typedef enum {
    HAL_ZIGBEE_DESCRIPTOR_OK,
    HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_COUNT,
    HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_ID,
    HAL_ZIGBEE_DESCRIPTOR_DUPLICATE_ENDPOINT,
    HAL_ZIGBEE_DESCRIPTOR_CLUSTER_CAPACITY,
    HAL_ZIGBEE_DESCRIPTOR_ATTRIBUTE_CAPACITY,
    HAL_ZIGBEE_DESCRIPTOR_NULL_POINTER,
    HAL_ZIGBEE_DESCRIPTOR_UNREGISTERED_CLUSTER,
    HAL_ZIGBEE_DESCRIPTOR_REGISTRATION_CAPACITY,
    HAL_ZIGBEE_DESCRIPTOR_DUPLICATE_CLUSTER_ID,
} hal_zigbee_descriptor_error_t;

typedef struct {
    uint32_t                      descriptor_validation_failures;
    hal_zigbee_descriptor_error_t last_descriptor_error;
} hal_zigbee_diagnostics_t;

/** Reserve cluster entries before constructing a multi-cluster endpoint. */
static inline bool hal_zigbee_endpoint_reserve_clusters(
    const hal_zigbee_endpoint *endpoint, uint8_t cluster_capacity,
    uint8_t cluster_count) {
    return endpoint != NULL && endpoint->clusters != NULL &&
           endpoint->cluster_capacity == cluster_capacity &&
           endpoint->cluster_count <= cluster_capacity &&
           cluster_count <= cluster_capacity - endpoint->cluster_count;
}

/** Append one cluster after validating the endpoint's assigned storage. */
static inline bool hal_zigbee_endpoint_add_cluster(
    hal_zigbee_endpoint *endpoint, uint8_t cluster_capacity,
    const hal_zigbee_cluster *cluster) {
    if (cluster == NULL || !hal_zigbee_endpoint_reserve_clusters(
            endpoint, cluster_capacity, 1)) {
        return false;
    }

    endpoint->clusters[endpoint->cluster_count] = *cluster;
    endpoint->cluster_count++;
    return true;
}

/** Validate descriptor storage before a platform copies it into fixed buffers. */
static inline hal_zigbee_descriptor_error_t
hal_zigbee_validate_descriptor_graph(const hal_zigbee_endpoint *endpoints,
                                     uint8_t endpoint_count,
                                     uint8_t endpoint_capacity,
                                     uint8_t endpoint_id_max,
                                     size_t cluster_capacity,
                                     size_t attribute_capacity) {
    bool   endpoint_seen[HAL_ZIGBEE_ENDPOINT_ID_MAX + 1] = { false };
    size_t cluster_count   = 0;
    size_t attribute_count = 0;

    if ((endpoints == NULL && endpoint_count != 0) ||
        endpoint_count > endpoint_capacity) {
        return HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_COUNT;
    }
    if (endpoint_id_max > HAL_ZIGBEE_ENDPOINT_ID_MAX) {
        return HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_ID;
    }
    for (uint8_t i = 0; i < endpoint_count; i++) {
        const hal_zigbee_endpoint *endpoint = &endpoints[i];

        if (endpoint->endpoint == 0 || endpoint->endpoint > endpoint_id_max) {
            return HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_ID;
        }
        if (endpoint_seen[endpoint->endpoint]) {
            return HAL_ZIGBEE_DESCRIPTOR_DUPLICATE_ENDPOINT;
        }
        endpoint_seen[endpoint->endpoint] = true;
        if (endpoint->cluster_count != 0 && endpoint->clusters == NULL) {
            return HAL_ZIGBEE_DESCRIPTOR_NULL_POINTER;
        }
        if (endpoint->cluster_count > endpoint->cluster_capacity) {
            return HAL_ZIGBEE_DESCRIPTOR_CLUSTER_CAPACITY;
        }
        cluster_count += endpoint->cluster_count;
        if (cluster_count > cluster_capacity) {
            return HAL_ZIGBEE_DESCRIPTOR_CLUSTER_CAPACITY;
        }
        for (uint8_t j = 0; j < endpoint->cluster_count; j++) {
            const hal_zigbee_cluster *cluster = &endpoint->clusters[j];

            if (cluster->attribute_count != 0 && cluster->attributes == NULL) {
                return HAL_ZIGBEE_DESCRIPTOR_NULL_POINTER;
            }
            if ((cluster->role != HAL_ZIGBEE_ROLE_SERVER &&
                 cluster->role != HAL_ZIGBEE_ROLE_CLIENT) ||
                (cluster->manufacturer_code != 0 &&
                 cluster->manufacturer_code == 0xFFFFu)) {
                return HAL_ZIGBEE_DESCRIPTOR_UNREGISTERED_CLUSTER;
            }
            for (uint8_t prior = 0; prior < j; prior++) {
                const hal_zigbee_cluster *other = &endpoint->clusters[prior];

                if (other->cluster_id == cluster->cluster_id &&
                    other->role == cluster->role &&
                    other->manufacturer_code == cluster->manufacturer_code) {
                    return HAL_ZIGBEE_DESCRIPTOR_UNREGISTERED_CLUSTER;
                }
            }
            for (uint8_t k = 0; k < cluster->attribute_count; k++) {
                const hal_zigbee_attribute *attribute = &cluster->attributes[k];
                if ((attribute->flag & ~(HAL_ATTR_READ | HAL_ATTR_WRITE |
                                         HAL_ATTR_REPORT)) != 0 ||
                    (attribute->flag & HAL_ATTR_REPORT &&
                     !(attribute->flag & HAL_ATTR_READ))) {
                    return HAL_ZIGBEE_DESCRIPTOR_UNREGISTERED_CLUSTER;
                }
            }
            attribute_count += cluster->attribute_count;
            if (attribute_count > attribute_capacity) {
                return HAL_ZIGBEE_DESCRIPTOR_ATTRIBUTE_CAPACITY;
            }
        }
    }
    return HAL_ZIGBEE_DESCRIPTOR_OK;
}

/** Platform resources one descriptor graph takes (see zigbee_limits.h). */
typedef struct {
    uint16_t endpoints;
    uint16_t server_clusters;
    uint16_t client_clusters;
    uint16_t attributes;
    uint16_t zcl_registrations;
    bool     has_ota;
} hal_zigbee_graph_usage_t;

/** Count one cluster. The OTA client takes one ZCL table entry per device. */
static inline void hal_zigbee_graph_usage_add_cluster(
    hal_zigbee_graph_usage_t *usage, uint16_t cluster_id,
    hal_zigbee_cluster_role_t role, uint16_t attribute_count) {
    if (role == HAL_ZIGBEE_ROLE_SERVER) {
        usage->server_clusters++;
    } else {
        usage->client_clusters++;
    }
    usage->attributes += attribute_count;
    if (cluster_id != HAL_ZIGBEE_CLUSTER_ID_OTA) {
        usage->zcl_registrations++;
    } else if (!usage->has_ota) {
        usage->has_ota = true;
        usage->zcl_registrations++;
    }
}

/** Resources of a graph whose storage passed the descriptor validation. */
static inline hal_zigbee_graph_usage_t hal_zigbee_graph_usage(
    const hal_zigbee_endpoint *endpoints, uint8_t endpoint_count) {
    hal_zigbee_graph_usage_t usage = { 0 };

    usage.endpoints = endpoint_count;
    for (uint8_t i = 0; i < endpoint_count; i++) {
        for (uint8_t j = 0; j < endpoints[i].cluster_count; j++) {
            const hal_zigbee_cluster *cluster = &endpoints[i].clusters[j];

            hal_zigbee_graph_usage_add_cluster(&usage, cluster->cluster_id,
                                               cluster->role,
                                               cluster->attribute_count);
        }
    }
    return usage;
}

/** Check graph resources against the shared platform limits. */
static inline hal_zigbee_descriptor_error_t
hal_zigbee_check_graph_usage(const hal_zigbee_graph_usage_t *usage) {
    if (usage->endpoints > HAL_ZIGBEE_MAX_ENDPOINTS) {
        return HAL_ZIGBEE_DESCRIPTOR_ENDPOINT_COUNT;
    }
    if (usage->server_clusters > HAL_ZIGBEE_MAX_SERVER_CLUSTERS ||
        usage->client_clusters > HAL_ZIGBEE_MAX_CLIENT_CLUSTERS ||
        usage->server_clusters + usage->client_clusters >
        HAL_ZIGBEE_MAX_CLUSTERS) {
        return HAL_ZIGBEE_DESCRIPTOR_CLUSTER_CAPACITY;
    }
    if (usage->attributes > HAL_ZIGBEE_MAX_ATTRIBUTES) {
        return HAL_ZIGBEE_DESCRIPTOR_ATTRIBUTE_CAPACITY;
    }
    if (usage->zcl_registrations > HAL_ZIGBEE_MAX_ZCL_REGISTRATIONS) {
        return HAL_ZIGBEE_DESCRIPTOR_REGISTRATION_CAPACITY;
    }
    return HAL_ZIGBEE_DESCRIPTOR_OK;
}

/**
 * Validate a graph against the shared platform limits. Every platform calls
 * this before it registers anything, so a graph fails the same way on all of
 * them. The Telink ZCL table keys clusters by endpoint and cluster id only,
 * so one endpoint cannot hold a cluster id twice, in any role.
 */
static inline hal_zigbee_descriptor_error_t hal_zigbee_validate_platform_graph(
    const hal_zigbee_endpoint *endpoints, uint8_t endpoint_count) {
    hal_zigbee_graph_usage_t      usage;
    hal_zigbee_descriptor_error_t error = hal_zigbee_validate_descriptor_graph(
        endpoints, endpoint_count, HAL_ZIGBEE_MAX_ENDPOINTS,
        HAL_ZIGBEE_ENDPOINT_ID_MAX, HAL_ZIGBEE_MAX_CLUSTERS,
        HAL_ZIGBEE_MAX_ATTRIBUTES);

    if (error != HAL_ZIGBEE_DESCRIPTOR_OK) return error;

    for (uint8_t i = 0; i < endpoint_count; i++) {
        for (uint8_t j = 0; j < endpoints[i].cluster_count; j++) {
            for (uint8_t prior = 0; prior < j; prior++) {
                if (endpoints[i].clusters[prior].cluster_id ==
                    endpoints[i].clusters[j].cluster_id) {
                    return HAL_ZIGBEE_DESCRIPTOR_DUPLICATE_CLUSTER_ID;
                }
            }
        }
    }
    usage = hal_zigbee_graph_usage(endpoints, endpoint_count);
    return hal_zigbee_check_graph_usage(&usage);
}

/**
 * Initialize Zigbee stack with device endpoints and clusters.
 * Registers nothing and returns false when the graph fails
 * hal_zigbee_validate_platform_graph or the stack refuses a registration;
 * the caller then initializes a smaller graph.
 * @param endpoints Array of endpoint definitions
 * @param endpoints_cnt Number of endpoints
 */
bool hal_zigbee_init(hal_zigbee_endpoint *endpoints, uint8_t endpoints_cnt);

hal_zigbee_diagnostics_t hal_zigbee_get_diagnostics(void);

// Network membership control

/**
 * Get current Zigbee network connection status
 * @return Network status (joined/joining/not joined)
 */
hal_zigbee_network_status_t hal_zigbee_get_network_status(void);

/** Function called when network status changes */
typedef void (*hal_network_status_change_callback_t)(
    hal_zigbee_network_status_t new_status);

/**
 * Register callback for network status changes
 * @param callback Function to call on status change
 */
void hal_register_on_network_status_change_callback(
    hal_network_status_change_callback_t callback);

/** Leave current Zigbee network */
void hal_zigbee_leave_network(void);

/** Start searching for and joining Zigbee networks (pairing mode) */
void hal_zigbee_start_network_steering(void);

/** Set Zigbee OTA image type */
void hal_zigbee_set_image_type(uint16_t image_type);

// Attributes

/**
 * Notify SDK that attribute value changed (for example, trigger reporting)
 * @param endpoint Endpoint number
 * @param cluster_id Cluster ID
 * @param attribute_id Attribute ID that changed
 */
void hal_zigbee_notify_attribute_changed(uint8_t endpoint, uint16_t cluster_id,
                                         uint16_t attribute_id);

typedef struct {
    uint8_t                endpoint;            // source endpoint
    uint16_t               profile_id;          // e.g., 0x0104 (HA)
    uint16_t               cluster_id;          // e.g., 0x0006 (On/Off)
    uint8_t                command_id;          // ZCL command id
    uint8_t                cluster_specific;    // 1 = cluster-specific, 0 = global
    hal_zigbee_direction_t direction;
    uint8_t                disable_default_rsp; // 1 = set “disable default response”
    uint16_t               manufacturer_code;   // 0 if not manufacturer-specific
    const uint8_t *        payload;             // raw payload (little-endian encoded)
    uint8_t                payload_len;
} hal_zigbee_cmd;

/** Zigbee operation result codes (success or failure reasons) */
typedef enum {
    HAL_ZIGBEE_OK = 0,
    HAL_ZIGBEE_ERR_NOT_JOINED,
    HAL_ZIGBEE_ERR_BAD_ARG,
    HAL_ZIGBEE_ERR_SEND_FAILED,
} hal_zigbee_status_t;

/**
 * Send command to bound devices (switches controlling lights)
 * @param cmd Command structure to send
 * @return HAL_ZIGBEE_OK on success, error code otherwise
 */
hal_zigbee_status_t hal_zigbee_send_cmd_to_bindings(const hal_zigbee_cmd *cmd);

/** Send a command directly to coordinator endpoint 1. */
hal_zigbee_status_t
hal_zigbee_send_cmd_to_coordinator(const hal_zigbee_cmd *cmd);

/**
 * Send attribute report to bound devices (notify of state changes)
 * @param endpoint Source endpoint
 * @param cluster_id Cluster containing the attribute
 * @param attr_id Attribute ID to report
 * @param zcl_type_id ZCL data type of the value
 * @param value Pointer to attribute value
 * @param value_len Size of value in bytes
 * @return HAL_ZIGBEE_OK on success, error code otherwise
 */
hal_zigbee_status_t
hal_zigbee_send_report_attr(uint8_t endpoint, uint16_t cluster_id,
                            uint16_t attr_id, uint8_t zcl_type_id,
                            const void *value, uint8_t value_len);

/** Send Zigbee "announce" command to notify other devices of our presence
 * @return HAL_ZIGBEE_OK on success, error code otherwise
 */
hal_zigbee_status_t hal_zigbee_send_announce(void);


/** Function called when any ZCL command is received, both generic and cluster-specific */
typedef void (*hal_zcl_activity_callback_t)();

/**
 * Register callback for ZCL activity from network
 * @param callback Function to call when ZCL commands are received
 *
 * Idea is to use this for battery device to control polling rate after incoming messages.
 */
void hal_zigbee_register_on_zcl_activity_callback(
    hal_zcl_activity_callback_t callback);


/** Set end device poll rate in milliseconds
 *
 * No-op for routers.
 */
void hal_zigbee_set_poll_rate_ms(uint32_t poll_rate_ms);

/** Get current end device poll rate in milliseconds
 *
 * Returns 0 for routers.
 */
uint32_t hal_zigbee_get_poll_rate_ms(void);

/** Find cluster definition by endpoint and cluster ID */
static inline hal_zigbee_cluster *
hal_zigbee_find_cluster(hal_zigbee_endpoint *endpoints, uint8_t endpoints_count,
                        uint8_t endpoint, uint16_t cluster_id) {
    if (!endpoints) {
        return NULL;
    }

    for (int i = 0; i < endpoints_count; i++) {
        if (endpoints[i].endpoint == endpoint) {
            for (int j = 0; j < endpoints[i].cluster_count; j++) {
                if (endpoints[i].clusters[j].cluster_id == cluster_id) {
                    return &endpoints[i].clusters[j];
                }
            }
        }
    }
    return NULL;
}

static inline hal_zigbee_cluster *hal_zigbee_find_cluster_exact(
    hal_zigbee_endpoint *endpoints, uint8_t endpoints_count, uint8_t endpoint,
    uint16_t cluster_id, hal_zigbee_cluster_role_t role,
    uint16_t manufacturer_code) {
    if (endpoints == NULL) {
        return NULL;
    }

    for (uint8_t i = 0; i < endpoints_count; i++) {
        if (endpoints[i].endpoint != endpoint) {
            continue;
        }
        for (uint8_t j = 0; j < endpoints[i].cluster_count; j++) {
            hal_zigbee_cluster *cluster = &endpoints[i].clusters[j];

            if (cluster->cluster_id == cluster_id && cluster->role == role &&
                cluster->manufacturer_code == manufacturer_code) {
                return cluster;
            }
        }
    }
    return NULL;
}

static inline hal_zigbee_attribute *hal_zigbee_find_attribute_in_cluster(
    hal_zigbee_cluster *cluster, uint16_t attribute_id) {
    if (cluster == NULL) {
        return NULL;
    }

    for (uint8_t i = 0; i < cluster->attribute_count; i++) {
        if (cluster->attributes[i].attribute_id == attribute_id) {
            return &cluster->attributes[i];
        }
    }
    return NULL;
}

static inline hal_zigbee_attribute *hal_zigbee_find_attribute_exact(
    hal_zigbee_endpoint *endpoints, uint8_t endpoints_count, uint8_t endpoint,
    uint16_t cluster_id, hal_zigbee_cluster_role_t role,
    uint16_t manufacturer_code, uint16_t attribute_id) {
    return hal_zigbee_find_attribute_in_cluster(
        hal_zigbee_find_cluster_exact(endpoints, endpoints_count, endpoint,
                                      cluster_id, role, manufacturer_code),
        attribute_id);
}

/** Find attribute definition within a specific cluster */
static inline hal_zigbee_attribute *
hal_zigbee_find_attribute(hal_zigbee_endpoint *endpoints,
                          uint8_t endpoints_count, uint8_t endpoint,
                          uint16_t cluster_id, uint16_t attribute_id) {
    hal_zigbee_cluster *cluster =
        hal_zigbee_find_cluster(endpoints, endpoints_count, endpoint, cluster_id);

    if (!cluster) {
        return NULL;
    }

    return hal_zigbee_find_attribute_in_cluster(cluster, attribute_id);
}

#endif
