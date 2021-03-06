all: build thrift

gen-cpp: bitbox.thrift *.cpp Makefile
	thrift --gen cpp bitbox.thrift
gen-py: bitbox.thrift *.cpp Makefile
	thrift --gen py bitbox.thrift
gen-php: bitbox.thrift *.cpp Makefile
	thrift --gen php bitbox.thrift

COMPILE_FLAGS=-O2 -Wall `pkg-config --cflags glib-2.0` \
	      -I. -Igen-cpp -Iliblzf-3.5 -I/usr/local/include/thrift

LINK_FLAGS=`pkg-config --libs glib-2.0` -lthrift -lthriftnb -levent

bitbox-server: gen-cpp bitbox.cc bitbox.h server.cpp Makefile
	gcc $(COMPILE_FLAGS) -c bitbox.cc -std=gnu++0x       -o bitbox.o
	gcc $(COMPILE_FLAGS) -c server.cpp -std=gnu++0x      -o server.o
	gcc $(COMPILE_FLAGS) -c gen-cpp/bitbox_constants.cpp -o bitbox_constants.o
	gcc $(COMPILE_FLAGS) -c gen-cpp/bitbox_types.cpp     -o bitbox_types.o
	gcc $(COMPILE_FLAGS) -c gen-cpp/Bitbox.cpp           -o Bitbox.o
	gcc $(COMPILE_FLAGS) -c liblzf-3.5/lzf_c.c           -o lzf_c.o
	gcc $(COMPILE_FLAGS) -c liblzf-3.5/lzf_d.c           -o lzf_d.o
	gcc $(COMPILE_FLAGS) -c sigh.c                       -o sigh.o
	gcc $(COMPILE_FLAGS) -c MurmurHash2_32_and_64.cpp    -o MurmurHash2_32_and_64.o
	gcc $(LINK_FLAGS) *.o -o bitbox-server

thrift: gen-cpp gen-py gen-php

build: bitbox-server

clean:
	rm -rf bitbox-server gen-cpp gen-py gen-php *.o
