.PHONY:clean

CXXFLAGS=-std=gnu++14 -ggdb3
LDLIBS=-lpthread

all: merge parse count

clean:
	rm -f parse merge
