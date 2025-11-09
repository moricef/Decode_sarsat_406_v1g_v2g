# Makefile for COSPAS-SARSAT 406 MHz Decoder with DSSS Support
# Licence Creative Commons CC BY-NC-SA

CC = gcc
CFLAGS = -Wall -Wextra -O3 -march=native -fopenmp -g
LDFLAGS = -lm -lgomp -lfftw3f

# Source files
COMMON_SRC = dec406_v2g.c display_utils.c country_codes.h
DSSS_SRC = dsss_demod_matlab.c
MAIN_SRC = dec406_main.c

# MATLAB Coder generated files
MATLAB_DIR = test_matlab_coder
MATLAB_SRCS = \
	$(MATLAB_DIR)/dsss_receiver.c \
	$(MATLAB_DIR)/dsss_receiver_emxutil.c \
	$(MATLAB_DIR)/dsss_receiver_initialize.c \
	$(MATLAB_DIR)/dsss_receiver_rtwutil.c \
	$(MATLAB_DIR)/dsss_receiver_terminate.c \
	$(MATLAB_DIR)/helperPolyphaseCorrelator.c \
	$(MATLAB_DIR)/minOrMax.c \
	$(MATLAB_DIR)/pskdemod.c \
	$(MATLAB_DIR)/rtGetInf.c \
	$(MATLAB_DIR)/rtGetNaN.c \
	$(MATLAB_DIR)/rt_nonfinite.c \
	$(MATLAB_DIR)/sign.c

MATLAB_OBJS = $(MATLAB_SRCS:.c=.o)

# Object files
COMMON_OBJ = $(COMMON_SRC:.c=.o)
DSSS_OBJ = $(DSSS_SRC:.c=.o)

# Executables
TARGETS = dec406_dsss_test

.PHONY: all clean check_deps

all: check_deps $(TARGETS)

# Check dependencies
check_deps:
	@echo "Checking dependencies..."
	@which $(CC) > /dev/null || (echo "ERROR: gcc not found" && exit 1)
	@pkg-config --exists fftw3f || (echo "ERROR: libfftw3f-dev not installed. Run: sudo apt-get install libfftw3-dev" && exit 1)
	@echo "All dependencies OK"

# DSSS test program (with MATLAB Coder)
dec406_dsss_test: test_dsss_main.o dsss_demod_matlab.o dec406_v2g.o display_utils.o $(MATLAB_OBJS)
	$(CC) $(CFLAGS) -I$(MATLAB_DIR) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

# Compilation rules
%.o: %.c
	$(CC) $(CFLAGS) -I$(MATLAB_DIR) -c $< -o $@

# Dependencies
dsss_demod_matlab.o: dsss_demod_matlab.c dsss_demod.h
	$(CC) $(CFLAGS) -I$(MATLAB_DIR) -c dsss_demod_matlab.c -o dsss_demod_matlab.o

dec406_v2g.o: dec406_v2g.c dec406.h display_utils.h country_codes.h
	$(CC) $(CFLAGS) -c dec406_v2g.c -o dec406_v2g.o

display_utils.o: display_utils.c display_utils.h
	$(CC) $(CFLAGS) -c display_utils.c -o display_utils.o

test_dsss_main.o: test_dsss_main.c dsss_demod.h dec406.h
	$(CC) $(CFLAGS) -c test_dsss_main.c -o test_dsss_main.o

# Clean
clean:
	rm -f *.o $(MATLAB_DIR)/*.o $(TARGETS)
	@echo "Cleaned build artifacts"

# Install dependencies (Debian/Ubuntu)
install-deps:
	@echo "Installing dependencies..."
	sudo apt-get update
	sudo apt-get install -y build-essential libfftw3-dev pkg-config
	@echo "Dependencies installed"

# Help
help:
	@echo "COSPAS-SARSAT 406 MHz DSSS Decoder Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all executables (default)"
	@echo "  clean            - Remove build artifacts"
	@echo "  check_deps       - Check if dependencies are installed"
	@echo "  install-deps     - Install required dependencies (requires sudo)"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Usage example:"
	@echo "  make                     # Build everything"
	@echo "  make clean               # Clean build"
	@echo "  ./dec406_dsss_test file.iq  # Test with IQ file"
