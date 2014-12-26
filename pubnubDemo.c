/* -*- c-file-style:"stroustrup" -*- */
#include "pubnub.h"

#include "contiki-net.h"
#include "dev/leds.h"

#include <stdio.h>
#include <string.h>

#if PUBNUB_USE_MDNS
#include "ip64.h"
#include "mdns.h"
#endif


/* If you need some configuration data, here's the
   place to put it
*/
static uip_ipaddr_t google_ipv4_dns_server = {
    .u8 = {
	/* Google's IPv4 DNS in mapped in an IPv6 address (::ffff:8.8.8.8) */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xff, 0xff,
	0x08, 0x08, 0x08, 0x08,
    }
};


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

#if PUBNUB_USE_MDNS
    ip64_init();
    mdns_conf(&google_ipv4_dns_server);
#endif

    /* Get a context and initialize it */
    m_pb = pubnub_get_ctx(0);
    pubnub_init(m_pb, pubkey, subkey);
    
    etimer_set(&et, 5*CLOCK_SECOND);
    
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
		leds_toggle(LEDS_ALL);
		printf("pubnubDemo: Received message: %s\n", msg);
	    }
	    etimer_restart(&et);
	}
    }

    PROCESS_END();
}
