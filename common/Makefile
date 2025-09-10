TARGET = libcommon.a

CROSS = arm-linux-gnueabihf-
CC = $(CROSS)gcc
AR = $(CROSS)ar
CFLAGS = -Wall -Werror -g -I../include -I./easylogger/plugins/file

SRCS = $(wildcard ./*.c ./easylogger/src/*.c ./easylogger/port/*.c ./easylogger/plugins/file/*.c ./wol/*.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) -rcs $@ $^
	cp $@ ../lib
	cp $^ ../output

clean:
	rm -rf $(OBJS) $(TARGET)
