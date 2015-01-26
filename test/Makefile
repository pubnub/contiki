PROJECT_SOURCEFILES += ../pubnub.c pubnubTestBasic.c pubnubTestMedium.c
CONTIKI_PROJECT = pubnubTest
all: $(CONTIKI_PROJECT)

#UIP_CONF_IPV6=1
TARGET = minimal-net
CONTIKI = ../contiki-2.7
include $(CONTIKI)/Makefile.include
CFLAGS += -D VERBOSE_DEBUG -D PUBNUB_USE_MDNS=0

