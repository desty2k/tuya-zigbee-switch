#ifndef BASE_COMPONENTS_GPIO_PROBE_CODES_H_
#define BASE_COMPONENTS_GPIO_PROBE_CODES_H_

/* Generated from helper_scripts/management_protocol.json. These values
 * travel over Zigbee unchanged. */

typedef enum {
    GPIO_PROBE_STATUS_OK = 0,
    GPIO_PROBE_STATUS_DISABLED,
    GPIO_PROBE_STATUS_BUSY,
    GPIO_PROBE_STATUS_BAD_PIN,
    GPIO_PROBE_STATUS_RANGE,
    GPIO_PROBE_STATUS_NOT_FLOATING,
    GPIO_PROBE_STATUS_NOT_ARMED,
    GPIO_PROBE_STATUS_VERIFY_FAILED,
    GPIO_PROBE_STATUS_RESOURCE,
} gpio_probe_status_t;

typedef enum {
    GPIO_PROBE_CLASS_UNTESTED = 0,
    GPIO_PROBE_CLASS_FLOATING,
    GPIO_PROBE_CLASS_DRIVEN_HIGH,
    GPIO_PROBE_CLASS_DRIVEN_LOW,
    GPIO_PROBE_CLASS_ACTIVE,
} gpio_probe_class_t;

typedef enum {
    GPIO_PROBE_PULL_NONE = 0,
    GPIO_PROBE_PULL_UP,
    GPIO_PROBE_PULL_DOWN,
} gpio_probe_pull_t;

#define GPIO_PROBE_STATE_PULL_TEST      (1u << 0)
#define GPIO_PROBE_STATE_WATCH          (1u << 1)
#define GPIO_PROBE_STATE_ARMED          (1u << 2)
#define GPIO_PROBE_STATE_PULSE          (1u << 3)
#define GPIO_PROBE_WATCH_RUNNING        (1u << 0)
#define GPIO_PROBE_WATCH_OVER_RATE      (1u << 1)

#define GPIO_PROBE_WATCH_MIN_S          1u
#define GPIO_PROBE_WATCH_MAX_S          300u
#define GPIO_PROBE_PULSE_MIN_MS         1u
#define GPIO_PROBE_PULSE_MAX_MS         500u
#define GPIO_PROBE_RATE_CEILING_HZ      20000u
#define GPIO_PROBE_ARM_TIMEOUT_MS       30000u
#define GPIO_PROBE_PULL_PHASE_MS        25u
#define GPIO_PROBE_WATCH_HEADER_SIZE    4u
#define GPIO_PROBE_WATCH_RECORD_SIZE    16u

#endif
