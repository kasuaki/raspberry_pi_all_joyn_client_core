#makefile

CXX:=/usr/bin/arm-linux-gnueabihf-g++

CXXFLAGS:=-std=c++11 -Wall -Werror -pipe -fno-exceptions -fno-strict-aliasing -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fdata-sections -Wno-long-long -Wno-deprecated -Wno-unknown-pragmas -Wunused-parameter -g -march=armv6 -mthumb-interwork -DROUTER -DQCC_OS_GROUP_POSIX -DQCC_OS_LINUX -D_GLIBCXX_USE_C99_FP_MACROS_DYNAMIC -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC 

LIBS:=lib/liballjoyn.a lib/libajrouter.a lib/liballjoyn_about.a -Llib -lajrouter -lalljoyn -lrt -lstdc++ -lpthread -lm -lcrypto -lssl -lwiringPi

INCLUDES:=-Iinc

.PHONY: default
default: all

.PHONY: all
all: Sensor

.PHONY: Sensor
Sensor: Sensor.cpp 
	$(CXX) -o $@ $< $(LIBS) $(CXXFLAGS) $(INCLUDES) 

.PHONY: clean
clean: 
	rm -f Sensor
