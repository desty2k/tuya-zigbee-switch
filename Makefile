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
tests: stub/build stub/build_end_device
	$(PYTHON) -m pytest tests/ -v

UNIT_BUILD_DIR := build/unit
UNIT_CFLAGS := -Wall -Wextra -Werror -std=c99 -DHAL_STUB -Isrc -Itests/unit
UNIT_SUPPORT := \
	tests/unit/support/fake_clock.c \
	tests/unit/support/fake_tasks.c \
	tests/unit/support/fake_gpio.c
UNIT_QUEUE_TEST := $(UNIT_BUILD_DIR)/test_gpio_edge_queue
UNIT_TIMER_TEST := $(UNIT_BUILD_DIR)/test_timer_service

$(UNIT_BUILD_DIR):
	mkdir -p $(UNIT_BUILD_DIR)

$(UNIT_QUEUE_TEST): tests/unit/test_gpio_edge_queue.c \
		src/base_components/gpio_edge_queue.c $(UNIT_SUPPORT) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

$(UNIT_TIMER_TEST): tests/unit/test_timer_service.c \
		src/base_components/timer_service.c $(UNIT_SUPPORT) | $(UNIT_BUILD_DIR)
	$(CC) $(UNIT_CFLAGS) $^ -o $@

unit_tests: $(UNIT_QUEUE_TEST) $(UNIT_TIMER_TEST)
	./$(UNIT_QUEUE_TEST)
	./$(UNIT_TIMER_TEST)

# Format all C/H files using uncrustify
format:
	find src -name '*.c' -o -name '*.h' | xargs uncrustify -c uncrustify.cfg --replace --no-backup

setup_venv:
	python3 -m venv .venv
	. .venv/bin/activate && pip install -r requirements.txt

setup: silabs/tools/all telink/tools/all setup_venv


# Define available targets for help
.PHONY: help setup setup_venv stub/% silabs/% telink/% tests unit_tests tools/% board/% format
