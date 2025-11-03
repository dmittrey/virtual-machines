.PHONY: all clean

all: l1-cache-info.cc opaque.cc
	g++ -O2 --std=c++17 l1-cache-info.cc opaque.cc -o l1-info

clean:
	rm -fr l1-info