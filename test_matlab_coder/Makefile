# Makefile for testing MATLAB Coder generated dsss_receiver

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I.
LDFLAGS = -lm

# Source files from MATLAB Coder (now local)
CODEGEN_SRCS = \
	dsss_receiver.c \
	dsss_receiver_emxutil.c \
	dsss_receiver_initialize.c \
	dsss_receiver_rtwutil.c \
	dsss_receiver_terminate.c \
	helperPolyphaseCorrelator.c \
	minOrMax.c \
	pskdemod.c \
	rtGetInf.c \
	rtGetNaN.c \
	rt_nonfinite.c \
	sign.c

# Our test main
TEST_SRC = main_test.c

# Object files
CODEGEN_OBJS = $(CODEGEN_SRCS:.c=.o)
TEST_OBJ = main_test.o

# Target executable
TARGET = test_matlab_receiver

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(TEST_OBJ) $(CODEGEN_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(TEST_OBJ) $(CODEGEN_OBJS)
	@echo "Cleaned build artifacts"

test: $(TARGET)
	./$(TARGET) /home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/tools/test_frame_sps8.sigmf-data
