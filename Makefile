# Spartan Makefile

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -O2

# Source files
SRCS = $(wildcard *.c)

# Object files
OBJS = $(SRCS:.c=.o)

# Binary output
TARGET = clox

# Default target
all: $(TARGET)

# Build the binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean intermediate files and the binary
clean:
	rm -f $(OBJS) $(TARGET)
