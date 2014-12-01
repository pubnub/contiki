PROJECT_SOURCEFILES += pubnub.c
CONTIKI_PROJECT = pubnubDemo
all: $(CONTIKI_PROJECT)

#UIP_CONF_IPV6=1
TARGET = minimal-net
CONTIKI = ..
include $(CONTIKI)/Makefile.include
