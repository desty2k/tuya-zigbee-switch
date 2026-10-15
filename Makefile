# Main Makefile - delegates to target-specific Makefiles

# Default target
help:
	@echo "Tuya Zigbee Switch"
	@echo "============================================="
	@echo ""
	@echo "Quick Start:"
	@echo "  make stub/build    - Build stub device for testing/simulation"
	@echo "  make stub/run      - Run stub device interactively"
	@echo "  make tests          - Run automated tests (builds stub first)"
	@echo ""
	@echo "Telink Development:"
	@echo "  make telink/tools/all - Download and install Telink tools"
	@echo "  make telink/build     - Build firmware for Telink TC32"
	@echo "  make telink/help      - Show Telink build system help"
	@echo ""
	@echo "Silabs Development:"
	@echo "  make silabs/tools/all - Download and install Silicon Labs tools"
	@echo "  make silabs/trust     - Trust SDK signature (first time setup)"
	@echo "  make silabs/gen       - Generate project files from .slcp"
	@echo "  make silabs/build     - Build firmware for EFR32MG21"
	@echo "  make silabs/install   - Flash firmware to device"
	@echo ""
	@echo "Stub/Simulation Development:"
	@echo "  make stub/help     - Show detailed stub commands"
	@echo "  make stub/build    - Build host-native simulation binary"
	@echo "  make stub/run      - Run stub device with REPL interface"
	@echo "  make stub/clean    - Clean stub build and NVM data"
	@echo ""
	@echo "Testing:"
	@echo "  make tests          - Run full pytest suite against stub device"
	@echo "  make unit_tests     - Run native C unit tests"
	@echo ""
	@echo "Bootloader:"
	@echo "  make silabs/bootloader_build   - Build bootloader"
	@echo "  make silabs/bootloader_install - Flash bootloader"
	@echo ""
	@echo "Tools:"
	@echo "  make tools/help                    - Show all available tools commands"
	@echo "  make tools/clean_z2m_index         - Clear all Zigbee2MQTT OTA index files"
	@echo "  make tools/update_converters       - Generate Z2M converter files"
	@echo "  make tools/update_zha_quirk        - Generate ZHA quirks file"
	@echo "  make tools/update_homed_extension  - Generate HOMEd extensions file"
	@echo "  make tools/update_supported_devices - Generate supported devices documentation"
	@echo "  make tools/freeze_ota_links        - Replace branch refs with commit hashes"
	@echo "  make tools/unused_image_type       - Show next available firmware image type ID"
	@echo "  make tools/check_device_features   - Check config tokens against board features"
	@echo ""
	@echo "Device-Specific Builds:"
	@echo "  BOARD=DEVICE_NAME make board/build - Build specific device from device_db.yaml"
	@echo ""
	@echo "Bulk Operations:"
	@echo "  make_scripts/make_all.sh     - Build all devices (CI pipeline equivalent)"
	@echo ""
	@echo "Setup and Environment:"
	@echo "  make setup           - Install all tools and setup Python venv"
	@echo "  make setup_venv      - Setup Python virtual environment only"
	@echo ""
	@echo "For detailed help on specific targets:"
	@echo "  make board/help        - Show device database build system help"
	@echo "  make silabs/help       - Show Silicon Labs build system help"
	@echo "  make silabs/tools/help - Show Silicon Labs tools help"
	@echo "  make telink/help       - Show Telink build system help" 
	@echo "  make telink/tools/help - Show Telink tools help"
	@echo "  make stub/help         - Show stub device build system help"
	@echo ""

stub/%:
	$(MAKE) -C src/stub $*

silabs/%:
	$(MAKE) -C src/silabs $*

telink/%:
	$(MAKE) -C src/telink $*

tools/%:
	$(MAKE) -f tools.mk $*

board/%:
	$(MAKE) -f board.mk $*

# Pick a Python interpreter for test runs. In some environments (notably WSL
# minimal installs), `python` may be missing while `python3` exists.
PYTHON ?= $(shell command -v python >/dev/null 2>&1 && echo python || echo python3)

# Run pytest tests (requires stub to be built)
tests: stub/build stub/build_end_device stub/build_switch_only stub/build_probe
	$(PYTHON) -m pytest tests/ -v

include src/features.mk

UNIT_BUILD_DIR := build/unit
UNIT_BASE_CFLAGS := -Wall -Wextra -Werror -std=c99 -DHAL_STUB -Isrc -Itests/unit
unit_feature_defines = $(addprefix -D,$(call feature_defines_for,$(1)))
UNIT_CFLAGS := $(UNIT_BASE_CFLAGS) $(call unit_feature_defines,$(ALL_FEATURES))
UNIT_SUPPORT := \
	tests/unit/support/fake_clock.c \
	tests/unit/support/fake_tasks.c \
	tests/unit/support/fake_gpio.c
UNIT_QUEUE_TEST := $(UNIT_BUILD_DIR)/test_gpio_edge_queue
UNIT_TIMER_TEST := $(UNIT_BUILD_DIR)/test_timer_service
UNIT_BUTTON_TEST := $(UNIT_BUILD_DIR)/test_button_input
UNIT_DEBOUNCE_FUZZ := $(UNIT_BUILD_DIR)/fuzz_debounce
UNIT_GESTURE_TEST := $(UNIT_BUILD_DIR)/test_gesture_fsm
UNIT_RELAY_TEST := $(UNIT_BUILD_DIR)/test_relay_controller
UNIT_INTERLOCK_TEST := $(UNIT_BUILD_DIR)/test_interlock
UNIT_BUTTON_EVENT_TEST := $(UNIT_BUILD_DIR)/test_button_event_queue
UNIT_DESCRIPTOR_TEST := $(UNIT_BUILD_DIR)/test_zigbee_descriptor
UNIT_TELINK_ZIGBEE_INIT_TEST := $(UNIT_BUILD_DIR)/test_telink_zigbee_init
UNIT_CONFIG_PARSER_TEST := $(UNIT_BUILD_DIR)/test_config_parser
UNIT_ACTION_MAPPER_TEST := $(UNIT_BUILD_DIR)/test_action_mapper
UNIT_CAPABILITY_STATE_TEST := $(UNIT_BUILD_DIR)/test_capability_state
UNIT_COVER_TEST := $(UNIT_BUILD_DIR)/test_cover_controller
UNIT_COUNTER_TEST := $(UNIT_BUILD_DIR)/test_gpio_counter
UNIT_METER_TEST := $(UNIT_BUILD_DIR)/test_meter_service
UNIT_PROBE_TEST := $(UNIT_BUILD_DIR)/test_gpio_probe_service
UNIT_PARSER_SWITCH_ONLY_TEST := $(UNIT_BUILD_DIR)/test_config_parser_switch_only
UNIT_PARSER_COVER_BATTERY_TEST := $(UNIT_BUILD_DIR)/test_config_parser_cover_battery
UNIT_PARSER_FEATURE_SOURCES := tests/unit/test_config_parser_features.c \
	src/device_config/config_parser.c \
	src/device_config/device_composition.c \
	tests/unit/support/fake_gpio_counter.c \
	tests/unit/support/fake_gpio_parse.c
UNIT_GPIO_PIN_TEST := $(UNIT_BUILD_DIR)/test_gpio_pin_mapping
TELINK_INIT_TEST_CFLAGS := $(UNIT_CFLAGS) -Itests/unit/telink_sdk_stub -Isrc/telink -Isrc/telink/hal

$(UNIT_BUILD_DIR):
	mkdir -p $(UNIT_BUILD_DIR)

$(UNIT_QUEUE_TEST): tests/unit/test_gpio_edge_queue.c \
		src/base_components/gpio_edge_queue.c $(UNIT_SUPPORT) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_TIMER_TEST): tests/unit/test_timer_service.c \
		src/base_components/timer_service.c $(UNIT_SUPPORT) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_BUTTON_TEST): tests/unit/test_button_input.c \
		src/base_components/button_input.c \
		src/base_components/button_dispatcher.c \
		src/base_components/gpio_edge_queue.c $(UNIT_SUPPORT) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_DEBOUNCE_FUZZ): tests/unit/fuzz_debounce.c \
		src/base_components/button_input.c \
		src/base_components/button_dispatcher.c \
		src/base_components/gpio_edge_queue.c $(UNIT_SUPPORT) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_GESTURE_TEST): tests/unit/test_gesture_fsm.c \
		src/base_components/gesture_fsm.c \
		src/base_components/timer_service.c \
		src/base_components/button_input.c \
		src/base_components/button_dispatcher.c \
		src/base_components/gpio_edge_queue.c $(UNIT_SUPPORT) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_RELAY_TEST): tests/unit/test_relay_controller.c \
		src/base_components/relay_controller.c \
		src/base_components/interlock.c \
		src/base_components/relay_driver.c \
		src/base_components/timer_service.c \
		tests/unit/support/fake_clock.c \
		tests/unit/support/fake_tasks.c \
		tests/unit/support/fake_relay_hw.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_INTERLOCK_TEST): tests/unit/test_interlock.c \
		src/base_components/relay_controller.c \
		src/base_components/relay_driver.c \
		src/base_components/interlock.c \
		src/base_components/timer_service.c \
		tests/unit/support/fake_clock.c \
		tests/unit/support/fake_tasks.c \
		tests/unit/support/fake_relay_hw.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_BUTTON_EVENT_TEST): tests/unit/test_button_event_queue.c \
		src/zigbee/button_event_cluster.c tests/unit/support/fake_tasks.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_DESCRIPTOR_TEST): tests/unit/test_zigbee_descriptor.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $< -o $@

$(UNIT_TELINK_ZIGBEE_INIT_TEST): tests/unit/test_telink_zigbee_init.c \
		src/telink/hal/zigbee.c | $(UNIT_BUILD_DIR)
	$(CC) $(TELINK_INIT_TEST_CFLAGS) $^ -o $@

$(UNIT_CONFIG_PARSER_TEST): tests/unit/test_config_parser.c \
		src/device_config/config_parser.c \
		src/device_config/device_composition.c \
		tests/unit/support/fake_gpio_counter.c \
		tests/unit/support/fake_gpio_parse.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_GPIO_PIN_TEST): tests/unit/test_gpio_pin_mapping.c src/hal/gpio_pin_text.h \
		src/telink/hal/telink_gpio_pin.h \
		src/silabs/hal/silabs_gpio_pin.h | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $< -o $@

$(UNIT_ACTION_MAPPER_TEST): tests/unit/test_action_mapper.c \
		src/base_components/action_mapper.c \
		src/base_components/relay_controller.c \
		src/base_components/interlock.c \
		src/base_components/relay_driver.c \
		src/base_components/timer_service.c \
		tests/unit/support/fake_clock.c \
		tests/unit/support/fake_tasks.c \
		tests/unit/support/fake_relay_hw.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_CAPABILITY_STATE_TEST): tests/unit/test_capability_state.c \
		src/device_config/capability_state.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_COVER_TEST): tests/unit/test_cover_controller.c \
		src/base_components/cover_controller.c \
		src/base_components/relay_controller.c \
		src/base_components/relay_driver.c \
		src/base_components/interlock.c \
		src/base_components/timer_service.c \
		tests/unit/support/fake_clock.c \
		tests/unit/support/fake_tasks.c \
		tests/unit/support/fake_relay_hw.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_COUNTER_TEST): tests/unit/test_gpio_counter.c \
		src/stub/hal/gpio_counter.c \
		tests/unit/support/fake_clock.c \
		tests/unit/support/fake_gpio.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_METER_TEST): tests/unit/test_meter_service.c \
		src/base_components/meter_service.c \
		src/base_components/meter_fake_driver.c \
		src/base_components/timer_service.c \
		tests/unit/support/fake_clock.c \
		tests/unit/support/fake_tasks.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_PROBE_TEST): tests/unit/test_gpio_probe_service.c \
		src/base_components/gpio_probe_service.c \
		src/stub/hal/gpio_probe.c \
		tests/unit/support/fake_clock.c \
		tests/unit/support/fake_tasks.c | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_PARSER_SWITCH_ONLY_TEST): $(UNIT_PARSER_FEATURE_SOURCES) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_BASE_CFLAGS) $(call unit_feature_defines,relay switch) $^ -o $@

$(UNIT_PARSER_COVER_BATTERY_TEST): $(UNIT_PARSER_FEATURE_SOURCES) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_BASE_CFLAGS) $(call unit_feature_defines,cover battery) $^ -o $@

unit_tests: $(UNIT_QUEUE_TEST) $(UNIT_TIMER_TEST) $(UNIT_BUTTON_TEST) \
		$(UNIT_DEBOUNCE_FUZZ) $(UNIT_GESTURE_TEST) $(UNIT_RELAY_TEST) \
		$(UNIT_INTERLOCK_TEST) $(UNIT_BUTTON_EVENT_TEST) $(UNIT_DESCRIPTOR_TEST) \
		$(UNIT_TELINK_ZIGBEE_INIT_TEST) $(UNIT_CONFIG_PARSER_TEST) $(UNIT_ACTION_MAPPER_TEST) $(UNIT_CAPABILITY_STATE_TEST) $(UNIT_COVER_TEST) $(UNIT_COUNTER_TEST) $(UNIT_METER_TEST) \
		$(UNIT_PROBE_TEST) $(UNIT_PARSER_SWITCH_ONLY_TEST) $(UNIT_PARSER_COVER_BATTERY_TEST) \
		$(UNIT_GPIO_PIN_TEST)
	./$(UNIT_QUEUE_TEST)
	./$(UNIT_TIMER_TEST)
	./$(UNIT_BUTTON_TEST)
	./$(UNIT_DEBOUNCE_FUZZ)
	./$(UNIT_GESTURE_TEST)
	./$(UNIT_RELAY_TEST)
	./$(UNIT_INTERLOCK_TEST)
	./$(UNIT_BUTTON_EVENT_TEST)
	./$(UNIT_DESCRIPTOR_TEST)
	./$(UNIT_TELINK_ZIGBEE_INIT_TEST)
	./$(UNIT_CONFIG_PARSER_TEST)
	./$(UNIT_ACTION_MAPPER_TEST)
	./$(UNIT_CAPABILITY_STATE_TEST)
	./$(UNIT_COVER_TEST)
	./$(UNIT_COUNTER_TEST)
	./$(UNIT_METER_TEST)
	./$(UNIT_PROBE_TEST)
	./$(UNIT_PARSER_SWITCH_ONLY_TEST)
	./$(UNIT_PARSER_COVER_BATTERY_TEST)
	./$(UNIT_GPIO_PIN_TEST)

# Format all C/H files using uncrustify
format:
	find src -name '*.c' -o -name '*.h' | xargs uncrustify -c uncrustify.cfg --replace --no-backup

setup_venv:
	python3 -m venv .venv
	. .venv/bin/activate && pip install -r requirements.txt

setup: silabs/tools/all telink/tools/all setup_venv


# Define available targets for help
.PHONY: help setup setup_venv stub/% silabs/% telink/% tests unit_tests tools/% board/% format
