CC     = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LIBS   = -lm

SRC_DIR = src
SRCS    = $(wildcard $(SRC_DIR)/*.c)
TARGET  = pulsedb

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

clean:
	rm -f $(TARGET)