all: build thrift

gen-cpp: bitbox.thrift *.cpp
	thrift --gen cpp bitbox.thrift
gen-py: bitbox.thrift *.cpp
	thrift --gen py bitbox.thrift
gen-php: bitbox.thrift *.cpp
	thrift --gen php bitbox.thrift

COMPILE_FLAGS=-Wall `pkg-config --cflags glib-2.0` -I. -Igen-cpp -I/usr/local/include/thrift
LINK_FLAGS=`pkg-config --libs glib-2.0` -lthrift

bitbox-server: gen-cpp bitbox.c bitbox.h server.cpp
	gcc $(COMPILE_FLAGS) -c bitbox.c -o bitbox.o
	gcc $(COMPILE_FLAGS) -c server.cpp -o server.o -std=gnu++0x
	gcc $(COMPILE_FLAGS) -c gen-cpp/bitbox_constants.cpp -o bitbox_constants.o
	gcc $(COMPILE_FLAGS) -c gen-cpp/bitbox_types.cpp -o bitbox_types.o
	gcc $(COMPILE_FLAGS) -c gen-cpp/Bitbox.cpp -o Bitbox.o
	gcc $(LINK_FLAGS) *.o -o bitbox-server

thrift: gen-cpp gen-py gen-php

build: bitbox-server

clean:
	rm -rf bitbox-server gen-cpp gen-py gen-php *.o
