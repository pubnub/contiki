/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */


#include "../pubnub.h"

#include "pubnubTest.h"

#include "contiki-net.h"
#include "dev/leds.h"
#include "dev/serial-line.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>

#if PUBNUB_USE_MDNS
#include "ip64.h"
#include "mdns.h"
#endif


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

/* So far, we have only one manual test, so we're putting it here,
   instead of a separatee module.
*/


TEST_DEF(basic_broken_conn)
{
    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    etimer_set(&g_et, 6*CLOCK_SECOND);
    pubnub_subscribe(g_pb, channel);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_publish(g_pb, channel, "\"Test 3\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_subscribe(g_pb, channel);
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);
    expect(got_messages(g_pb, "\"Test 3\"", NULL));

    pubnub_publish(g_pb, channel, "\"Test 3 - 2\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);

    printf("Please disconnect from Internet. Press Enter when done.");
    yield_expect_ev(serial_line_event_message);

    pubnub_subscribe(g_pb, channel);
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_TIMEOUT);

    printf("Please reconnect to Internet. Press Enter when done.");
    yield_expect_ev(serial_line_event_message);

    pubnub_subscribe(g_pb, channel);
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);
    expect(got_messages(g_pb, "\"Test 3 - 2\"", NULL));

    printf("Please disconnect from Internet. Press Enter when done.");
    yield_expect_ev(serial_line_event_message);

    pubnub_publish(g_pb, channel, "\"Test 3 - 3\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_TIMEOUT);
    
    printf("Please reconnect to Internet. Press Enter when done.");
    yield_expect_ev(serial_line_event_message);

    pubnub_publish(g_pb, channel, "\"Test 3 - 4\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_subscribe(g_pb, channel);
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);
    expect(got_messages(g_pb, "\"Test 3 - 4\"", NULL));
}
TEST_ENDDEF



static struct TestData m_aTest[] = {
    LIST_TEST(basic_broken_conn)
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
