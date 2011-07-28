targets = peer #holepoked

# Put holepoke.pb.o first so it gets built before the other objects, which need it's .h file
holepoked_objs = holepoke.pb.o network.o uuid.o node_peer.o #holepoked.o
peer_objs = holepoke.pb.o endpoint.o network.o fsm.o peer.o #sender.o receiver.o

PROTOC=protoc

UNAME=$(shell uname)

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
LDFLAGS+=-luuid
CC=gcc
CXX=g++
LD=g++
endif

LDFLAGS+=-lstdc++ -lpthread -lm -lprotobuf

CFLAGS+=$(INCLUDES)
CXXFLAGS=$(CFLAGS)

all: $(holepoked_objs)

%.pb.cc: %.proto
	$(PROTOC) -I=. --cpp_out=. $<

#holepoked: $(holepoked_objs)
#	$(LD) -v -o $@ $(holepoked_objs) $(LDFLAGS)

peer: $(peer_objs)
	$(LD) -o $@ $(peer_objs) $(LDFLAGS)

clean:
	$(RM) $(targets) $(holepoked_objs) $(peer_objs) holepoke.pb.h
