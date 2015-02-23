PROJECT_SOURCEFILES += pubnub.c pubnub_ccore.c
CONTIKI_PROJECT = pubnubDemo
all: test $(CONTIKI_PROJECT)
#all: $(CONTIKI_PROJECT)

#UIP_CONF_IPV6=1
TARGET = minimal-net
CONTIKI = ./contiki-2.7
include $(CONTIKI)/Makefile.include
CFLAGS += -D VERBOSE_DEBUG -D PUBNUB_USE_MDNS=0

unittest: pubnub.c pubnub.h pubnub.t.c
	gcc -o pubnub.t.so -shared $(CFLAGS) -Wall -fprofile-arcs -ftest-coverage -fPIC pubnub.c pubnub_ccore.c pubnub.t.c -lcgreen -lm
	valgrind --quiet cgreen-runner ./pubnub.t.so

