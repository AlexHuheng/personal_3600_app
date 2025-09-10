TARGET = h3600_app

ifeq ($(H3600_DEBUG), TRUE) 
	CFLAGS += -DH3600_DEBUG
	TARGET = h3600_debug
endif 

INPUT_VERSION = "1.0.09"
INPUT_SHELL_VERSION = "$(shell date +'%y%m%d')"

CROSS = arm-linux-gnueabihf-

THRIFT_DIR = ../thrift_slave
CC = $(CROSS)gcc
STRIP = $(CROSS)strip
CFLAGS += -Wall -Werror -g -I./include -I../include -I$(THRIFT_DIR)/ -I../common/wol -DINPUT_VERSION=\"$(INPUT_VERSION)\" -DINPUT_SHELL_VERSION=\"$(INPUT_SHELL_VERSION)\"
LDFLAGS += -L../lib -lpthread -lthrift -lstdc++ -lm -lev -ljansson -lrt

SRCS = $(wildcard ./*.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))
OTHER_OBJS = $(wildcard ../output/*.o)

$(TARGET): $(OBJS) $(OTHER_OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
	#$(STRIP) $@
	cp $@  ~/tftpboot

all: $(TARGET)

clean:
	rm -rf $(TARGET) $(OBJS) $(OTHER_OBJS)


