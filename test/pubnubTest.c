/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */


#include "../pubnub.h"

#include "pubnubTestBasic.h"
#include "pubnubTestMedium.h"

#include "contiki-net.h"
#include "dev/leds.h"

#if PUBNUB_USE_MDNS
#include "ip64.h"
#include "mdns.h"
#endif

#include <stdio.h>
#include <stdbool.h>



#if PUBNUB_USE_MDNS

#define pubnub_dns_event mdns_event_found

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

#else

#define pubnub_dns_event resolv_event_found

#endif



const char pubkey[] = "demo";
const char subkey[] = "demo";
//const char channel[] = "hello_world";
const char channel[] = "ch";


PROCESS(pubnub_test, "PubNub Test process");
AUTOSTART_PROCESSES(&pubnub_test, &pubnub_process);

struct pubnub *g_pb;
struct pubnub *g_pb_2;
struct etimer g_et;





struct pt *g_current_pt;




typedef char (*PF_Test_T)(process_event_t ev, process_data_t data, enum TestResult *pResult);

struct TestData {
    PF_Test_T pf;
    struct pt *pPT;
};

#define LIST_TEST(tstname) { test_##tstname, &pt_test_##tstname }

static struct TestData m_aTest[] = {
    LIST_TEST(basic_conn_pub),
    LIST_TEST(basic_conn_rx),
    LIST_TEST(medium_conn_pub),
    LIST_TEST(medium_conn_rx),
    LIST_TEST(medium_complex_rx_tx),
    LIST_TEST(medium_conn_disc_conn_again),
    LIST_TEST(medium_wrong_api),
    LIST_TEST(medium_cloud_err)
};

#define TEST_COUNT (sizeof m_aTest / sizeof m_aTest[0])


PROCESS_THREAD(pubnub_test, ev, data)
{
    static int s_iTest = 0;

    PROCESS_BEGIN();

#if PUBNUB_USE_MDNS
    ip64_init();
    mdns_conf(&google_ipv4_dns_server);
#endif

    leds_init();

    /* A little wait to start. If using DHCP, this waits for it to
       finish - unfortunately, IP64 DHCPv4 does not send an event
       when done, so we have to guess how much time it's gonna take.
    */
    PT_INIT(m_aTest[0].pPT);
    etimer_set(&g_et, 10*CLOCK_SECOND);
    printf("\n\n\x1b[34m Starting automatic tests in 10s\x1b[m\n");

    while (1) {
	enum TestResult test_result;

	PROCESS_WAIT_EVENT();

	if (ev == pubnub_dns_event) {
	    continue;
	}
        if ((0 == m_aTest[s_iTest].pPT->lc) && (ev = PROCESS_EVENT_TIMER)) {
	    printf("\x1b[34m Starting %d. test of %ld\x1b[m\n", s_iTest + 1, TEST_COUNT);
        }
	leds_on(LEDS_ALL);
	if (0 == PT_SCHEDULE(m_aTest[s_iTest].pf(ev, data, &test_result))) {
            switch (test_result) {
	    case trFail:
		/** Test failed */
		printf("\n\x1b[41m !!!!!!! The %d test failed!\x1b[m\n\n", s_iTest + 1);
		for (;;) {
		    int i;
		    leds_on(LEDS_ALL);
		    for (i = 0; i < 16000; ++i) {
			continue;
		    }
		    leds_off(LEDS_ALL);
		}
		break;
	    case trPass:
		if (++s_iTest >= TEST_COUNT) {
		    /** All tests passed */
		    printf("\x1b[32m All tests passed\x1b[m\n");
		    for (;;) {
			leds_off(LEDS_ALL);
		    }
		}
		PT_INIT(m_aTest[s_iTest].pPT);
                etimer_set(&g_et, CLOCK_SECOND/2);
		break;
            case trIndeterminate:
                etimer_set(&g_et, 2*CLOCK_SECOND);
		printf("\x1b[33m ReStarting %d. test of %ld\x1b[m\t", s_iTest + 1, TEST_COUNT);
                break;
            }
	}
    }

    PROCESS_END();
}
