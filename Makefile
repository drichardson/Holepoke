UNAME=$(shell uname)
PROTOC=protoc

TARGETS = holepoke.a

# Put holepoke.pb.o first so it gets built before the other objects, which need it's .h file
OBJS = holepoke.pb.o fsm.o endpoint.o network.o node_peer.o

ifdef DEBUGON
CFLAGS=-Wall -Werror -g -O0 -fno-inline -DDEBUGON
else
CFLAGS=-Wall -O3
endif

ifeq ($(UNAME), Darwin)
macports_prefix=/opt/local
INCLUDES+=-I$(macports_prefix)/include
LDFLAGS+=-L$(macports_prefix)/lib -framework CoreFoundation -arch i386 -arch x86_64 -lprotobuf
CFLAGS+=-arch i386 -arch x86_64
CC=clang
CXX=clang++
LD=clang++
endif

ifeq ($(UNAME), Linux)
CC=gcc
CXX=g++
LD=g++
endif

LDFLAGS+=-lstdc++ -lpthread -lm -lprotobuf

CFLAGS+=$(INCLUDES)
CXXFLAGS=$(CFLAGS)

all: holepoke.a

%.pb.cc: %.proto
	$(PROTOC) -I=. --cpp_out=. $<

holepoke.a: $(OBJS)
	ar -rcs $@ $^

clean:
	$(RM) $(OBJS) $(TARGETS) *.o *.pb.h
