# Firmware features compiled into an image.
#
# FEATURES lists the features to build (board.mk passes the board's list from
# helper_scripts/board_features.py). Each feature adds its sources, relative to
# src/, and sets FEATURE_<NAME> to 1; every other feature is set to 0, and the
# config parser rejects its tokens with CONFIG_PARSE_FEATURE_MISSING.
# Keep ALL_FEATURES in step with the `features` enum in device_db.schema.json.
#
# `interlock` is relay interlock: `K` groups and the relay pair of each cover.
# helper_scripts/board_features.py adds it to every board that can use it.
#
# `probe` is the GPIO probe image for discovering the pins of a device that
# cannot be opened (docs/contribute/porting.md). It is Telink-only, never in
# DEFAULT_FEATURES, and the platform makefiles add its HAL source.

ALL_FEATURES := relay switch cover battery meter probe interlock
DEFAULT_FEATURES := relay switch cover battery meter interlock
FEATURES ?= $(DEFAULT_FEATURES)

# The meter counters and the probe use the same TLSR8258 GPIO-to-RISC sources.
ifneq ($(filter meter,$(FEATURES)),)
ifneq ($(filter probe,$(FEATURES)),)
$(error Features meter and probe cannot share an image)
endif
endif

# Cover motor relays are always interlocked.
ifneq ($(filter cover,$(FEATURES)),)
ifeq ($(filter interlock,$(FEATURES)),)
$(error Feature cover needs feature interlock)
endif
endif

UNKNOWN_FEATURES := $(filter-out $(ALL_FEATURES),$(FEATURES))
ifneq ($(UNKNOWN_FEATURES),)
$(error Unknown FEATURES: $(UNKNOWN_FEATURES); known: $(ALL_FEATURES))
endif

FEATURE_SOURCES_relay := zigbee/relay_cluster.c zigbee/group_cluster.c
FEATURE_SOURCES_switch := zigbee/switch_cluster.c
FEATURE_SOURCES_cover := \
	base_components/cover_controller.c \
	zigbee/cover_cluster.c \
	zigbee/cover_switch_cluster.c
FEATURE_SOURCES_battery := base_components/battery.c zigbee/battery_cluster.c
FEATURE_SOURCES_meter := \
	base_components/meter_service.c \
	base_components/energy_measurement/bl0937.c \
	zigbee/electrical_measurement_cluster.c \
	zigbee/metering_cluster.c \
	zigbee/meter_calibration_cluster.c
FEATURE_SOURCES_interlock := base_components/interlock.c
FEATURE_SOURCES_probe := base_components/gpio_probe_service.c zigbee/gpio_probe_cluster.c

FEATURE_SOURCES := $(foreach feature,$(FEATURES),$(FEATURE_SOURCES_$(feature)))

# $(call feature_defines_for,<features>): NAME=0|1 pairs for every known
# feature; platform makefiles turn them into -D flags or SLC configuration
# items.
feature_defines_for = \
	FEATURE_RELAY=$(if $(filter relay,$(1)),1,0) \
	FEATURE_SWITCH=$(if $(filter switch,$(1)),1,0) \
	FEATURE_COVER=$(if $(filter cover,$(1)),1,0) \
	FEATURE_BATTERY=$(if $(filter battery,$(1)),1,0) \
	FEATURE_METER=$(if $(filter meter,$(1)),1,0) \
	FEATURE_PROBE=$(if $(filter probe,$(1)),1,0) \
	FEATURE_INTERLOCK=$(if $(filter interlock,$(1)),1,0)

FEATURE_DEFINES := $(call feature_defines_for,$(FEATURES))
