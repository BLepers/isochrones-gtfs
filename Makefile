.PHONY:clean

CXXFLAGS=-std=gnu++14 -ggdb3
LDLIBS=-lpthread

all: merge parse

clean:
	rm -f parse merge
