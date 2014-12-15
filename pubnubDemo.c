/* -*- c-file-style:"stroustrup" -*- */
#include "pubnub.h"
#include "contiki-net.h"

#include <stdio.h>


static const char pubkey[] = "demo";
static const char subkey[] = "demo";
static const char channel[] = "hello_world";


PROCESS(pubnub_demo, "PubNub Demo process");
AUTOSTART_PROCESSES(&pubnub_demo, &pubnub_process);

struct pubnub *m_pb;


PROCESS_THREAD(pubnub_demo, ev, data)
{
    static struct etimer et;
    PROCESS_BEGIN();

    /* Could try DHCP, but for now configure by hand... */
    {
	uip_ipaddr_t addr;
	uip_ipaddr(&addr, 208,67,222,222);
	resolv_conf(&addr);
    }
    
    m_pb = pubnub_get_ctx(0);
    pubnub_init(m_pb, pubkey, subkey);
    
    etimer_set(&et, 10*CLOCK_SECOND);
    
    while (1) {
	PROCESS_WAIT_EVENT();
	if (ev == PROCESS_EVENT_TIMER) {
	    printf("pubnubDemo: Timer\n");
	    pubnub_publish(m_pb, channel, "\"ConTiki Pubnub voyager\"");
	}
	else if (ev == pubnub_publish_event) {
	    printf("pubnubDemo: Publish event\n");
	    pubnub_subscribe(m_pb, channel);
	}
	else if (ev == pubnub_subscribe_event) {
	    printf("pubnubDemo: Subscribe event\n");
	    for (;;) {
		char const *msg = pubnub_get(m_pb);
		if (NULL == msg) {
		    break;
		}
		printf("pubnubDemo: Received message: %s\n", msg);
	    }
	    etimer_restart(&et);
	}
    }

    PROCESS_END();
}
