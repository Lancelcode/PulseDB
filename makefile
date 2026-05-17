CC     = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -g

SRC_DIR = src
SRCS    = $(wildcard $(SRC_DIR)/*.c)
TARGET  = pulsedb

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)