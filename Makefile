#CXXFLAGS=-Wall -g3 -std=gnu++11
CXXFLAGS=-Wall -O3 -fomit-frame-pointer -std=gnu++11
mysql_json.so: CXXFLAGS += -shared -fPIC
LDFLAGS=-s

libdir.x86_64 = /usr/lib64
libdir.i386   = /usr/lib
MACHINE := $(shell uname -i)
libdir = $(libdir.$(MACHINE))

mysql_json.so: mysql_json.cc

install: mysql_json.so
	install -p $^ $(libdir)/mysql/plugin

%.so: %.cc
	$(LINK.cc) $^ $(LOADLIBES) $(LDLIBS) -o $@
