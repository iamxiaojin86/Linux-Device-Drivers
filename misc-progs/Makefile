
#FILES = nbtest load50 mapcmp polltest mapper setlevel setconsole inp outp \
#	datasize dataalign netifdebug

FILES = setlevel setconsole nbtest asynctest load50 inp outp datasize dataalign

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
INCLUDEDIR = $(KERNELDIR)/include
CFLAGS = -O2 -fomit-frame-pointer -Wall -I$(INCLUDEDIR)

all: $(FILES)

clean:
	rm -f $(FILES) *~ core

