all: build thrift

gen-cpp: bitbox.thrift *.cpp Makefile
	thrift --gen cpp bitbox.thrift
gen-py: bitbox.thrift *.cpp Makefile
	thrift --gen py bitbox.thrift
gen-php: bitbox.thrift *.cpp Makefile
	thrift --gen php bitbox.thrift

COMPILE_FLAGS=-ggdb -Wall `pkg-config --cflags glib-2.0` \
	      -I. -Igen-cpp -Iliblzf-3.5 -I/usr/local/include/thrift

LINK_FLAGS=`pkg-config --libs glib-2.0` -lthrift

bitbox-server: gen-cpp bitbox.cc bitbox.h server.cpp Makefile
	g++ $(COMPILE_FLAGS) -c bitbox.cc -o bitbox.o -std=gnu++0x
	gcc $(COMPILE_FLAGS) -c server.cpp -o server.o
	g++ $(COMPILE_FLAGS) -c gen-cpp/bitbox_constants.cpp -o bitbox_constants.o
	g++ $(COMPILE_FLAGS) -c gen-cpp/bitbox_types.cpp -o bitbox_types.o
	g++ $(COMPILE_FLAGS) -c gen-cpp/Bitbox.cpp -o Bitbox.o
	gcc $(COMPILE_FLAGS) -c liblzf-3.5/lzf_c.c -o lzf_c.o
	gcc $(COMPILE_FLAGS) -c liblzf-3.5/lzf_d.c -o lzf_d.o
	gcc $(COMPILE_FLAGS) -c sigh.c -o sigh.o
	gcc $(LINK_FLAGS) *.o -o bitbox-server

thrift: gen-cpp gen-py gen-php

build: bitbox-server

clean:
	rm -rf bitbox-server gen-cpp gen-py gen-php *.o
