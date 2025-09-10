TARGET = libthrift_slave.a

CROSS = arm-linux-gnueabihf-
CXX = $(CROSS)g++ -std=c++11
AR  = $(CROSS)ar
CXXFLAGS = -Wall -Werror -g -I../include -I./gen-cpp -I../app/include -I./handler_cpp -I.

SRCS = $(wildcard ./*.cpp ./gen-cpp/*.cpp ./handler_cpp/*.cpp)
OBJS = $(patsubst %.cpp,%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) -rcs $@ $^
	cp $@ ../lib
	cp $^ ../output

clean:
	rm -rf $(OBJS) $(TARGET)
