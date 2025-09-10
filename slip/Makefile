TARGET = libslip.a
CROSS = arm-linux-gnueabihf-
CC = $(CROSS)gcc
AR = $(CROSS)ar
CFLAGS = -Wall -Werror -g -I../include

SRCS = $(wildcard ./*.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) -rcs $@ $^
	cp $@ ../lib
	cp $^ ../output

clean:
	rm -rf $(OBJS) $(TARGET)

