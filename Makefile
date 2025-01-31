CC = gcc
CFLAGS = -Wall -Wextra -g -I/usr/include/freetype2
LIBS = -llume -lm
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
TARGET = imp

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
