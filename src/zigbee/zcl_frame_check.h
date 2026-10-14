#ifndef ZIGBEE_ZCL_FRAME_CHECK_H_
#define ZIGBEE_ZCL_FRAME_CHECK_H_

/*
 * Bounds check for received ZCL foundation (profile-wide) command payloads.
 *
 * The Telink SDK parsers (zcl_parseInWriteCmd, zcl_parseInReportCmd,
 * zcl_parseInReadRspCmd, zcl_parseInCfgReportCmd,
 * zcl_parseInReadReportCfgRspCmd) take every record's length from the frame
 * without checking it against the frame. They sum the lengths in 16 bits and
 * allocate that many bytes, so a long string prefix of 0xFFFD wraps the
 * allocation to a few bytes and the copy then overruns the heap. Any node on
 * the network can send these commands. The check below rejects every frame
 * whose records do not end exactly at the end of the payload before such a
 * parser runs. The type sizes match the SDK's zcl_getDataTypeLen() and
 * zcl_analogDataType().
 */

#include <stdbool.h>
#include <stdint.h>

#define ZCL_FRAME_CMD_READ_RSP               0x01u
#define ZCL_FRAME_CMD_WRITE                  0x02u
#define ZCL_FRAME_CMD_WRITE_UNDIVIDED        0x03u
#define ZCL_FRAME_CMD_WRITE_NO_RSP           0x05u
#define ZCL_FRAME_CMD_CONFIG_REPORT          0x06u
#define ZCL_FRAME_CMD_READ_REPORT_CFG_RSP    0x09u
#define ZCL_FRAME_CMD_REPORT                 0x0Au

#define ZCL_FRAME_SEND_ATTR_REPORTS          0x00u
#define ZCL_FRAME_STATUS_SUCCESS             0x00u
/* The SDK counts records in a u8. */
#define ZCL_FRAME_MAX_RECORDS                255u

/** Size of a fixed-size ZCL type; 0 for types without a fixed size. */
static inline uint8_t zcl_frame_type_size(uint8_t type) {
    if (type == 0x10u) return 1;                              // boolean

    if (type >= 0x08u && type <= 0x0Fu) return type - 0x07u;  // data8..data64

    if (type >= 0x18u && type <= 0x1Fu) return type - 0x17u;  // bitmap8..bitmap64

    if (type >= 0x20u && type <= 0x27u) return type - 0x1Fu;  // uint8..uint64

    if (type >= 0x28u && type <= 0x2Fu) return type - 0x27u;  // int8..int64

    switch (type) {
    case 0x30u: return 1;  // enum8

    case 0x31u:            // enum16
    case 0x38u:            // semi-precision
    case 0xE8u:            // cluster id
    case 0xE9u: return 2;  // attribute id

    case 0x39u:            // single precision
    case 0xE1u:            // date
    case 0xE2u:            // UTC time
    case 0xEAu: return 4;  // BACnet OID

    case 0x3Au:            // double precision
    case 0xF0u: return 8;  // IEEE address

    case 0xF1u: return 16; // 128-bit key

    default: return 0;     // includes time of day, as in the SDK
    }
}

/** Types whose Configure Reporting record carries a reportable change. */
static inline bool zcl_frame_type_is_analog(uint8_t type) {
    return (type >= 0x20u && type <= 0x2Fu) || type == 0x38u || type == 0x39u ||
           type == 0x3Au || type == 0xE0u || type == 0xE1u || type == 0xE2u;
}

/**
 * Size of the value of `type` at `data`, with `left` bytes remaining.
 * Returns false when the value, or its length prefix, runs past the payload,
 * and for structured types the SDK cannot size safely.
 */
static inline bool zcl_frame_value_size(uint8_t type, const uint8_t *data,
                                        uint16_t left, uint16_t *size) {
    uint32_t value_size;

    if (type == 0x41u || type == 0x42u) { // octet / character string
        if (left < 1) return false;

        value_size = 1u + data[0];
    } else if (type == 0x43u || type == 0x44u) { // long octet / character string
        if (left < 2) return false;

        value_size = 2u + ((uint32_t)data[0] | ((uint32_t)data[1] << 8));
    } else if (type == 0x48u || type == 0x4Cu || type == 0x50u || type == 0x51u) {
        return false; // array, structure, set, bag
    } else {
        value_size = zcl_frame_type_size(type);
    }
    if (value_size > left) return false;

    *size = (uint16_t)value_size;
    return true;
}

/* attribute id (2), type (1), value */
static inline bool zcl_frame_attribute_records_are_bounded(const uint8_t *data,
                                                           uint16_t len) {
    uint16_t records = 0;

    while (len != 0) {
        uint16_t size;

        if (len < 3 || ++records > ZCL_FRAME_MAX_RECORDS ||
            !zcl_frame_value_size(data[2], data + 3, len - 3, &size)) return false;

        data += 3u + size;
        len  -= 3u + size;
    }
    return true;
}

/* attribute id (2), status (1), on success: type (1), value */
static inline bool zcl_frame_read_rsp_is_bounded(const uint8_t *data, uint16_t len) {
    uint16_t records = 0;

    while (len != 0) {
        uint16_t size = 0;

        if (len < 3 || ++records > ZCL_FRAME_MAX_RECORDS) return false;

        if (data[2] != ZCL_FRAME_STATUS_SUCCESS) {
            data += 3;
            len  -= 3;
            continue;
        }
        if (len < 4 || !zcl_frame_value_size(data[3], data + 4, len - 4, &size)) {
            return false;
        }
        data += 4u + size;
        len  -= 4u + size;
    }
    return true;
}

/* Reporting configuration body after direction and attribute id:
 * send: type (1), min (2), max (2), reportable change for analog types;
 * receive: timeout (2). Returns the body size or 0 when it runs past `len`. */
static inline uint16_t zcl_frame_report_cfg_size(uint8_t direction,
                                                 const uint8_t *data, uint16_t len) {
    uint16_t size;

    if (direction != ZCL_FRAME_SEND_ATTR_REPORTS) return len >= 2 ? 2 : 0;

    if (len < 5) return 0;

    size = 5;
    if (zcl_frame_type_is_analog(data[0])) size += zcl_frame_type_size(data[0]);
    return size <= len ? size : 0;
}

/* direction (1), attribute id (2), configuration */
static inline bool zcl_frame_config_report_is_bounded(const uint8_t *data, uint16_t len,
                                                      uint16_t max_records) {
    uint16_t records = 0;

    while (len != 0) {
        uint16_t size;

        if (len < 3 || ++records > max_records) return false;

        size = zcl_frame_report_cfg_size(data[0], data + 3, len - 3);
        if (size == 0) return false;

        data += 3u + size;
        len  -= 3u + size;
    }
    return true;
}

/* status (1), direction (1), attribute id (2), on success: configuration */
static inline bool zcl_frame_read_report_cfg_rsp_is_bounded(const uint8_t *data,
                                                            uint16_t len) {
    uint16_t records = 0;

    while (len != 0) {
        uint16_t size = 0;

        if (len < 4 || ++records > ZCL_FRAME_MAX_RECORDS) return false;

        if (data[0] == ZCL_FRAME_STATUS_SUCCESS) {
            size = zcl_frame_report_cfg_size(data[1], data + 4, len - 4);
            if (size == 0) return false;
        }
        data += 4u + size;
        len  -= 4u + size;
    }
    return true;
}

/**
 * True when a foundation command's payload can be parsed by the SDK without
 * reading or writing past it. Commands without variable-length records pass.
 * `max_config_report_records` is the number of Configure Reporting records
 * whose parsed form fits the SDK's 8-bit allocation size.
 */
static inline bool zcl_frame_is_well_formed(uint8_t command_id, const uint8_t *data,
                                            uint16_t len,
                                            uint16_t max_config_report_records) {
    if (len != 0 && data == 0) return false;

    switch (command_id) {
    case ZCL_FRAME_CMD_WRITE:
    case ZCL_FRAME_CMD_WRITE_UNDIVIDED:
    case ZCL_FRAME_CMD_WRITE_NO_RSP:
    case ZCL_FRAME_CMD_REPORT:
        return zcl_frame_attribute_records_are_bounded(data, len);

    case ZCL_FRAME_CMD_READ_RSP:
        return zcl_frame_read_rsp_is_bounded(data, len);

    case ZCL_FRAME_CMD_CONFIG_REPORT:
        return zcl_frame_config_report_is_bounded(data, len, max_config_report_records);

    case ZCL_FRAME_CMD_READ_REPORT_CFG_RSP:
        return zcl_frame_read_report_cfg_rsp_is_bounded(data, len);

    default:
        return true;
    }
}

#endif
