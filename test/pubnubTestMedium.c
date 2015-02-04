#include "pubnubTestBasic.h"

#include "../pubnub.h"


TEST_DEF(medium_conn_pub)

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    etimer_set(&g_et, 5*CLOCK_SECOND);
    pubnub_subscribe(g_pb, channel);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);
	
    pubnub_publish(g_pb, "ch", "\"Test M1\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);

	pubnub_publish(g_pb, "two", "\"Test M1\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);
    pubnub_subscribe(g_pb, "ch");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);
    expect(got_messages(g_pb, "\"Test M1\"", NULL));

    pubnub_subscribe(g_pb, "two");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);
    expect(got_messages(g_pb, "\"Test M1\"", NULL));

TEST_ENDDEF


TEST_DEF(medium_conn_rx)

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    pubnub_subscribe(g_pb, "ch");
    etimer_set(&g_et, 5*CLOCK_SECOND);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_publish(g_pb, "ch", "\"Test M2\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_publish(g_pb, "two", "\"Test M2 - 2\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(g_pb, PNR_OK);

    pubnub_subscribe(g_pb, "ch,two");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(g_pb, PNR_OK);
    expect(got_messages(g_pb, "\"Test M2\"", "\"Test M2 - 2\"", NULL));
    /* should probably check that M2 is on ch and M2 - 2 on two... */

TEST_ENDDEF


TEST_DEF(medium_complex_rx_tx)

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);
    g_pb_2 = pubnub_get_ctx(1);
    pubnub_init(g_pb_2, pubkey, subkey);

    expect(PNR_STARTED == pubnub_subscribe(g_pb, "two,tree"));
    expect(PNR_STARTED == pubnub_subscribe(g_pb_2, "ch"));
    etimer_set(&g_et, 5*CLOCK_SECOND);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);
    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);

    pubnub_publish(g_pb, "ch", "\"Test M3\"");
    pubnub_publish(g_pb_2, "tree", "\"Test M3 - 1\"");
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(data, PNR_OK);
	yield_expect_ev(pubnub_publish_event);
    expect_last_result(data, PNR_OK);

    pubnub_publish(g_pb, "two", "\"Test M3\"");
    etimer_restart(&g_et);
	yield_expect_ev(pubnub_publish_event);
	expect_last_result(data, PNR_OK);

    expect(PNR_STARTED == pubnub_subscribe(g_pb, "two,tree"));
    expect(PNR_STARTED == pubnub_subscribe(g_pb_2, "ch"));
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);
	if (data == g_pb) {
	    expect(got_messages(data, "\"Test M3\"", "\"Test M3 - 1\"", NULL));
	}
	else {
	    expect(got_messages(data, "\"Test M3\"", NULL));
	}
    /* should probably check that M3 is on two (or tree) and M3 - 1 on ch. */

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);
	if (data == g_pb) {
		expect(got_messages(data, "\"Test M3\"", "\"Test M3 - 1\"", NULL));
	}
	else {
		expect(got_messages(data, "\"Test M3\"", NULL));
	}

TEST_ENDDEF


TEST_DEF(medium_conn_disc_conn_again)

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test M4\""));
    pubnub_cancel(g_pb);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(data, PNR_CANCELLED);

    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));
    etimer_set(&g_et, 5*CLOCK_SECOND);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test M4 - 2\""));
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(data, PNR_OK);

    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);
    expect(got_messages(data, "\"Test M4 - 2\"", NULL));

    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));
    pubnub_cancel(g_pb);
    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_CANCELLED);

    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test M4 - 3\""));
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_publish_event);
    expect_last_result(data, PNR_OK);

    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));
    etimer_restart(&g_et);

    yield_expect_ev(pubnub_subscribe_event);
    expect_last_result(data, PNR_OK);
    expect(got_messages(data, "\"Test M4 - 3\"", NULL));

TEST_ENDDEF


TEST_DEF(medium_wrong_api)

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test \""));
    expect(PNR_IN_PROGRESS == pubnub_publish(g_pb, channel, "\"Test - 2\""));
    expect(PNR_IN_PROGRESS == pubnub_subscribe(g_pb, channel));
    
	pubnub_cancel(g_pb);
	yield_expect_ev(pubnub_publish_event);
	expect_last_result(data, PNR_CANCELLED);
	
    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));
    expect(PNR_IN_PROGRESS == pubnub_publish(g_pb, channel, "\"Test - 3\""));
    expect(PNR_IN_PROGRESS == pubnub_subscribe(g_pb, channel));

	pubnub_cancel(g_pb);
	yield_expect_ev(pubnub_subscribe_event);
	expect_last_result(data, PNR_CANCELLED);

TEST_ENDDEF


TEST_DEF(medium_cloud_err)

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

	expect(PNR_STARTED == pubnub_publish(g_pb, "ch,two", "0"));
	yield_expect_ev(pubnub_publish_event);
	expect_last_result(data, PNR_HTTP_ERROR);
	expect(pubnub_last_http_code(data) == 400);

	expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\""));
	yield_expect_ev(pubnub_publish_event);
	expect_last_result(data, PNR_HTTP_ERROR);
	expect(pubnub_last_http_code(data) == 400);
	
TEST_ENDDEF
