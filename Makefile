# Makefile for COSPAS-SARSAT 406 MHz Decoder
# Support FGB (1G) BPSK et SGB (2G) DSSS/OQPSK
# Licence Creative Commons CC BY-NC-SA

CC = gcc
CFLAGS = -Wall -Wextra -O2 -march=native -g -Iinclude
LDFLAGS = -lm
LDFLAGS_DSSS = -lm -fopenmp -lgomp -lfftw3f

HACKRF_HEADER := $(firstword $(wildcard /usr/include/libhackrf/hackrf.h /usr/local/include/libhackrf/hackrf.h))
ifeq ($(HACKRF_HEADER),)
HAVE_HACKRF := 0
else
HAVE_HACKRF := 1
endif

HACKRF_SRCS =
HACKRF_DEFS =
HACKRF_LIBS =
ifeq ($(HAVE_HACKRF),1)
HACKRF_SRCS = $(SRC_DIR)/backend_hackrf.c
HACKRF_DEFS = -DHAVE_HACKRF
HACKRF_LIBS = -lhackrf
endif

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
UTILS_DIR = utils

# Executables
TARGETS = \
	$(BUILD_DIR)/dec406_hex \
	$(BUILD_DIR)/dec406_audio \
	$(BUILD_DIR)/dec406_iq \
	$(BUILD_DIR)/dec406_dsss_test \
	$(BUILD_DIR)/dec406_scan \
	$(BUILD_DIR)/reset_usb

ifneq ($(wildcard $(UTILS_DIR)/generate_2g_hex.c),)
TARGETS += $(BUILD_DIR)/generate_2g_hex
endif

.PHONY: all clean check_deps help

all: check_deps $(TARGETS)

# Check dependencies
check_deps:
	@echo "Checking dependencies..."
	@which $(CC) > /dev/null || (echo "ERROR: gcc not found" && exit 1)
	@echo "All basic dependencies OK"
	@pkg-config --exists fftw3f 2>/dev/null || echo "WARNING: libfftw3f-dev not installed. DSSS programs will fail to link."

# ============================================================================
# FGB (1G) BPSK Programs
# ============================================================================

# dec406_hex - Decode from hex string
$(BUILD_DIR)/dec406_hex: $(SRC_DIR)/dec406_hex.c $(SRC_DIR)/dec406.c $(SRC_DIR)/dec406_v1g.c $(SRC_DIR)/dec406_v2g.c $(SRC_DIR)/display_utils.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

# dec406_audio - Decode from audio (WAV file or stdin)
$(BUILD_DIR)/dec406_audio: $(SRC_DIR)/main_audio.c $(SRC_DIR)/audio_capture.c $(SRC_DIR)/dec406.c $(SRC_DIR)/dec406_v1g.c $(SRC_DIR)/dec406_v2g.c $(SRC_DIR)/display_utils.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

# ============================================================================
# SGB (2G) DSSS/OQPSK Programs
# ============================================================================

# Pure-C DSSS demodulator sources
DSSS_SRCS = \
	$(SRC_DIR)/dsss_demod.c \
	$(SRC_DIR)/rrc_filter.c \
	$(SRC_DIR)/symbol_sync.c \
	$(SRC_DIR)/costas4.c \
	$(SRC_DIR)/despread.c \
	$(SRC_DIR)/freq_acq.c \

# dec406_iq - Decode 2G from IQ file (pure-C DSSS chain)
$(BUILD_DIR)/dec406_iq: $(SRC_DIR)/main_iq.c $(SRC_DIR)/dec406.c $(SRC_DIR)/dec406_v1g.c $(SRC_DIR)/dec406_v2g.c $(SRC_DIR)/display_utils.c $(DSSS_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

# dec406_dsss_test - Test DSSS demodulator
$(BUILD_DIR)/dec406_dsss_test: $(SRC_DIR)/test_dsss_main.c $(SRC_DIR)/dec406.c $(SRC_DIR)/dec406_v1g.c $(SRC_DIR)/dec406_v2g.c $(SRC_DIR)/display_utils.c $(DSSS_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

# test_acq_lag - Monte-Carlo validation of the acquisition lag-search cap
$(BUILD_DIR)/test_acq_lag: $(UTILS_DIR)/test_acq_lag.c $(SRC_DIR)/freq_acq.c $(SRC_DIR)/despread.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lfftw3f
	@echo "Built: $@"

# sgb_epl_diag - Offline SGB preamble EPL/Prompt diagnostics for cf32 windows
$(BUILD_DIR)/sgb_epl_diag: $(UTILS_DIR)/sgb_epl_diag.c $(SRC_DIR)/freq_acq.c $(SRC_DIR)/despread.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lfftw3f
	@echo "Built: $@"

# ============================================================================
# Real-time scanner (unified FGB + SGB)
# ============================================================================

# Common scanner sources
SCANNER_SRCS = \
	$(SRC_DIR)/scanner.c \
	$(SRC_DIR)/dec406.c \
	$(SRC_DIR)/dec406_v1g.c \
	$(SRC_DIR)/dec406_v2g.c \
	$(SRC_DIR)/display_utils.c \
	$(SRC_DIR)/fgb_iq_demod.c \
	$(SRC_DIR)/scan_alert.c \
	$(DSSS_SRCS)

# dec406_scan - Unified scanner with auto hardware detection (Airspy → RTL-SDR → PlutoSDR)
$(BUILD_DIR)/dec406_scan: $(SRC_DIR)/main_scan_unified.c $(SRC_DIR)/backend_rtlsdr.c $(SRC_DIR)/backend_airspy.c $(SRC_DIR)/backend_pluto.c $(HACKRF_SRCS) $(SCANNER_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DHAVE_RTLSDR -DHAVE_AIRSPY -DHAVE_PLUTO $(HACKRF_DEFS) -o $@ $(SRC_DIR)/main_scan_unified.c $(SRC_DIR)/backend_rtlsdr.c $(SRC_DIR)/backend_airspy.c $(SRC_DIR)/backend_pluto.c $(HACKRF_SRCS) $(SCANNER_SRCS) $(LDFLAGS) -lpthread -lrtlsdr -lairspy -liio $(HACKRF_LIBS)
	@echo "Built: $@ (Airspy + RTL-SDR + PlutoSDR$(if $(filter 1,$(HAVE_HACKRF)), + HackRF))"

# dec406_scan_rtlsdr - RTL-SDR only (legacy standalone)
$(BUILD_DIR)/dec406_scan_rtlsdr: $(SRC_DIR)/main_scan.c $(SRC_DIR)/dec406.c $(SRC_DIR)/dec406_v1g.c $(SRC_DIR)/dec406_v2g.c $(SRC_DIR)/display_utils.c $(SRC_DIR)/audio_capture.c $(SRC_DIR)/fgb_iq_demod.c $(SRC_DIR)/scan_alert.c $(DSSS_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lpthread -lrtlsdr
	@echo "Built: $@"

# dec406_scan_airspy - Airspy only (legacy standalone)
$(BUILD_DIR)/dec406_scan_airspy: $(SRC_DIR)/main_scan_airspy.c $(SRC_DIR)/dec406.c $(SRC_DIR)/dec406_v1g.c $(SRC_DIR)/dec406_v2g.c $(SRC_DIR)/display_utils.c $(SRC_DIR)/audio_capture.c $(SRC_DIR)/fgb_iq_demod.c $(SRC_DIR)/scan_alert.c $(DSSS_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lpthread -lairspy
	@echo "Built: $@"

# ============================================================================
# Utility Programs
# ============================================================================

# generate_2g_hex - Generate 2G test frame in hex
$(BUILD_DIR)/generate_2g_hex: $(UTILS_DIR)/generate_2g_hex.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

# reset_usb - USB device reset utility (for scan406.pl)
$(BUILD_DIR)/reset_usb: $(UTILS_DIR)/reset_usb.c
	@mkdir -p $(BUILD_DIR)
	$(CC) -o $@ $^
	@echo "Built: $@"

# ============================================================================
# Maintenance
# ============================================================================

clean:
	rm -rf $(BUILD_DIR)/*
	@echo "Cleaned build artifacts"

# Install dependencies (Debian/Ubuntu)
install-deps:
	@echo "Installing dependencies..."
	sudo apt-get update
	sudo apt-get install -y build-essential libfftw3-dev pkg-config
	@echo "Dependencies installed"

# Help
help:
	@echo "COSPAS-SARSAT 406 MHz Decoder Makefile"
	@echo "======================================="
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all executables (default)"
	@echo "  clean            - Remove build artifacts"
	@echo "  check_deps       - Check if dependencies are installed"
	@echo "  install-deps     - Install required dependencies (requires sudo)"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Executables:"
	@echo "  dec406_hex       - FGB decoder from hex string"
	@echo "  dec406_audio     - FGB decoder from audio (WAV/stdin)"
	@echo "  dec406_iq        - SGB decoder from IQ file (pure-C DSSS chain)"
	@echo "  dec406_dsss_test - Test DSSS demodulator"
	@echo "  dec406_scan      - Real-time FGB+SGB band scanner (rtl_sdr)"
	@echo "  generate_2g_hex  - Generate 2G test frame"
	@echo ""
	@echo "Usage examples:"
	@echo "  make                                    # Build all"
	@echo "  make clean                              # Clean build"
	@echo "  ./build/dec406_hex 1AD050B7D8A06F     # Decode hex FGB"
	@echo "  ./build/dec406_audio capture.wav        # Decode WAV FGB"
	@echo "  ./build/dec406_iq file.iq               # Decode SGB from IQ"
	@echo "  ./build/generate_2g_hex                 # Generate 2G frame"
	@echo ""
