#include "pubnubTestBasic.h"

#include "../pubnub.h"


TEST_DEF(basic_conn_pub)

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    etimer_set(&g_et, 5*CLOCK_SECOND);
    pubnub_subscribe(g_pb, channel);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_publish(g_pb, channel, "\"Test 1\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_publish(g_pb, channel, "\"Test 1 - 2\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_subscribe(g_pb, channel);
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);
    expect(got_messages(g_pb, "\"Test 1\"", "\"Test 1 - 2\"", NULL));

TEST_ENDDEF


TEST_DEF(basic_conn_rx)

	static bool s_subscribe_received;
	static bool s_publish_received;
	
    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);
    g_pb_2 = pubnub_get_ctx(1);
    pubnub_init(g_pb_2, pubkey, subkey);

    expect(PNR_STARTED == pubnub_subscribe(g_pb_2, channel));
    etimer_set(&g_et, 5*CLOCK_SECOND);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);

    expect(PNR_STARTED == pubnub_subscribe(g_pb_2, channel));
    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test 2\""));
    etimer_restart(&g_et);

	s_subscribe_received = s_publish_received = false;
	while (!s_subscribe_received || !s_publish_received) {
	    TEST_YIELD();

		if (ev == pubnub_subscribe_event) {
			expect(!s_subscribe_received);
			s_subscribe_received = true;
			expect_last_result(data, PNR_OK);
			expect(got_messages(data, "\"Test 2\"", NULL));
		}
		else {
			expect_ev(pubnub_publish_event);
			expect(!s_publish_received);
			s_publish_received = true;
			expect_last_result(data, PNR_OK);
		}
	}

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test 2 - 2\""));
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(data, PNR_OK);

    expect(PNR_STARTED == pubnub_subscribe(g_pb_2, channel));

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);
    expect(got_messages(data, "\"Test 2 - 2\"", NULL));

TEST_ENDDEF
