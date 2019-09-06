#
# When building a package or installing otherwise in the system, make
# sure that the variable PREFIX is defined, e.g. make PREFIX=/usr/local
#
PROGNAME=rbfeeder
DEBUG_LEVEL=0

ifndef FFARCH
FFARCH=$(shell cat arch)
endif

RBFEEDER_VERSION=0.2.20

ifndef DUMP1090_VERSION
DUMP1090_VERSION=rbfeeder-$(RBFEEDER_VERSION)
endif

ifdef PREFIX
BINDIR=$(PREFIX)/bin
SHAREDIR=$(PREFIX)/share/$(PROGNAME)
EXTRACFLAGS=-DHTMLPATH=\"$(SHAREDIR)\"
endif

ifeq ($(FFARCH), rbcs)
RBCS=1
RBCSRBLC=1
endif
ifeq ($(FFARCH), rblc)
RBLC=1
RBCSRBLC=1
endif

ifdef RBCS
EXTRA_LIBS=-lgps
endif

ifdef DEBUG_RELEASE
#CPPFLAGS+=-DDEBUG_RELEASE=1
CFLAGS+=-DDEBUG_RELEASE=1 -g
endif


ifndef CONN_KEY
$(error ************  Missing CONN_KEY define ************)
endif

ifndef CONN_NONCE
$(error ************  Missing CONN_NONCE define ************)
endif

ifndef DEF_XOR_KEY
$(error ************  Missing DEF_XOR_KEY define ************)
endif


CPPFLAGS+=-DMODES_DUMP1090_VERSION=\"$(DUMP1090_VERSION)\" -DAIRNAV_VER_PRINT=\"$(RBFEEDER_VERSION)\"
CFLAGS+=-O3 $(USER_DEFINES) -DDEF_CONN_KEY='$(CONN_KEY)' -DDEF_CONN_NONCE='$(CONN_NONCE)' -DDEFAULT_XOR_KEY='$(DEF_XOR_KEY)' -Wno-unused-result -Wall -Werror -W -Wno-unused-variable -Wno-unused-function -Wno-sign-compare -Wno-unused-but-set-variable -Wl,-z,relro,-z,now `pkg-config --cflags --libs glib-2.0` `pkg-config --cflags --libs libusb-1.0`
LIBS=-lpthread -lm $(EXTRA_LIBS)
LIBS_RTL=-L/usr/lib64 -L/usr/local/lib -L/usr/lib64 -L/lib64 -L/usr/local/lib -lrtlsdr -lusb-1.0 -lcurl `pkg-config --cflags --libs glib-2.0`
CC=gcc
#`pkg-config --libs librtlsdr libusb-1.0`

CFLAGS+=-DF_ARCH=\"$(FFARCH)\"

ifdef RBCSRBLC
CFLAGS+=-DRBCSRBLC
endif
ifdef RBCS
CFLAGS+=-DRBCS
endif
ifdef RBLC
CFLAGS+=-DRBLC
endif

UNAME := $(shell uname)

# -lrt  removido
ifeq ($(UNAME), Linux)
LIBS+=
CFLAGS+=-std=gnu99 -DDEBUG_LEVEL=$(DEBUG_LEVEL)
endif
ifeq ($(UNAME), Darwin)
UNAME_R := $(shell uname -r)
ifeq ($(shell expr "$(UNAME_R)" : '1[012345]\.'),3)
CFLAGS+=-std=c11 -DMISSING_GETTIME -DMISSING_NANOSLEEP
COMPAT+=compat/clock_gettime/clock_gettime.o compat/clock_nanosleep/clock_nanosleep.o
else
# Darwin 16 (OS X 10.12) supplies clock_gettime() and clockid_t
CFLAGS+=-std=c11 -DMISSING_NANOSLEEP -DCLOCKID_T
COMPAT+=compat/clock_nanosleep/clock_nanosleep.o
endif
endif

ifeq ($(UNAME), OpenBSD)
CFLAGS+= -DMISSING_NANOSLEEP
COMPAT+= compat/clock_nanosleep/clock_nanosleep.o
endif

all: rbfeeder

%.o: %.c *.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTRACFLAGS) -c $< -o $@

dump1090.o: CFLAGS += `pkg-config --cflags librtlsdr libusb-1.0`
	

rbfeeder: dump1090.o anet.o interactive.o mode_ac.o mode_s.o net_io.o crc.o demod_2400.o stats.o cpr.o icao_filter.o track.o util.o convert.o airnav.o airnav_utils.o md5.o salsa20.o $(COMPAT)
	$(CC) -g -o $@ $^ $(LIBS) $(LIBS_RTL) $(LDFLAGS)


rbmonitor: rbmonitor.o $(COMPAT)
	$(CC) -g -o rbmonitor $^ $(LIBS) $(LDFLAGS)

temperature: 
	$(CC) -o temperature temperature.c

clean:
	rm -f *.o rbfeeder rbmonitor temperature

