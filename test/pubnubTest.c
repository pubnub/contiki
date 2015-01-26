/* -*- c-file-style:"stroustrup" -*- */
#include "../pubnub.h"

#include "contiki-net.h"
#include "dev/leds.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#if PUBNUB_USE_MDNS
#include "ip64.h"
#include "mdns.h"
#endif

#include "pubnubTestBasic.h"
#include "pubnubTestMedium.h"


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


const char pubkey[] = "demo";
const char subkey[] = "demo";
//const char channel[] = "hello_world";
const char channel[] = "ch";


PROCESS(pubnub_test, "PubNub Test process");
AUTOSTART_PROCESSES(&pubnub_test, &pubnub_process);

struct pubnub *g_pb;
struct pubnub *g_pb_2;
struct etimer g_et;


/** Helper function to translate Pubnub result to a string */
static char const* pubnub_res_2_string(enum pubnub_res e)
{
    switch (e) {
    case PNR_OK: return "OK";
    case PNR_TIMEOUT: return "Timeout";
    case PNR_IO_ERROR: return "I/O (communication) error";
    case PNR_HTTP_ERROR: return "HTTP error received from server";
    case PNR_FORMAT_ERROR: return "Response format error";
    case PNR_CANCELLED: return "Pubnub API transaction cancelled";
    case PNR_STARTED: return "Pubnub API transaction started";
    case PNR_IN_PROGRESS: return "Pubnub API transaction already in progress";
    case PNR_RX_BUFF_NOT_EMPTY: return "Rx buffer not empty";
    case PNR_TX_BUFF_TOO_SMALL:  return "Tx buffer too small for sending/publishing the message";
    default: return "!?!?!";
    }
}



struct pt *g_current_pt;


bool got_messages(pubnub_t *p, ...)
{
    char const *aMsgs[16];
    uint16_t missing;
    size_t count = 0;
    va_list vl;

    va_start(vl, p);
    while (count < 16) {
	char const *msg = va_arg(vl, char*);
	if (NULL == msg) {
	    break;
	}
	aMsgs[count++] = msg;
    }
    va_end(vl);

    if ((0 == count) || (count > 16)) {
	return false;
    }

    missing = (0x01 << count) - 1;
    while (missing) {
	size_t i;
	char const *msg = pubnub_get(p);
	if (NULL == msg) {
	    break;
	}
	for (i = 0; i < count; ++i) {
	    if (strcmp(msg, aMsgs[i]) == 0) {
		missing &= ~(0x01 << i);
		break;
	    }
	}
    }

    return missing;
}


typedef char (*PF_Test_T)(process_event_t ev, process_data_t data, enum TestResult *pResult);

struct TestData {
    PF_Test_T pf;
    struct pt *pPT;
};

#define DECL_TEST(tstname) { tstname, &pt_##tstname }

static struct TestData m_aTest[] = {
    DECL_TEST(test_basic_1),
    DECL_TEST(test_basic_2),
    DECL_TEST(test_medium_1),
    DECL_TEST(test_medium_2),
    DECL_TEST(test_medium_3),
    DECL_TEST(test_medium_4),
    DECL_TEST(test_medium_wrong_api),
    DECL_TEST(test_medium_cloud_err)
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

    /* A little wait to start */
    PT_INIT(m_aTest[0].pPT);
    etimer_set(&g_et, 5*CLOCK_SECOND);
    printf("Starting automatic tests\n");
    
    while (1) {
	enum TestResult test_result;

	PROCESS_WAIT_EVENT();

	leds_on(LEDS_ALL);
	if (0 == PT_SCHEDULE(m_aTest[s_iTest].pf(ev, data, &test_result))) {
	    if (trFail == test_result) {
		/** Test failed */
		printf("Test %d failed\n", s_iTest);
		for (;;) {
		    leds_blink();
		}
	    }
	    else {
		if (++s_iTest >= TEST_COUNT) {
		    /** All tests passed */
		    printf("All tests passed\n");
		    for (;;) {
			leds_off(LEDS_ALL);
		    }
		}
		printf("Starting test %d\n", s_iTest);
		PT_INIT(m_aTest[s_iTest].pPT);
	    }
	}
    }

    PROCESS_END();
}
