#include "pubnubTestBasic.h"

#include "../pubnub.h"


struct pt pt_test_basic_1;


PT_THREAD(test_basic_1(process_event_t ev, process_data_t data, enum TestResult *pResult))
{
    TEST_BEGIN(pt_test_basic_1);

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    pubnub_publish(g_pb, channel, "\"Test 1\"");
    etimer_set(&g_et, 5*CLOCK_SECOND);
    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);

    pubnub_publish(g_pb, channel, "\"Test 1 - 2\"");
    etimer_restart(&g_et);
    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);

    pubnub_subscribe(g_pb, channel);
    etimer_restart(&g_et);
    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);
    expect(got_messages(g_pb, "\"Test 1\"", "\"Test 1 - 2\"", NULL));

    TEST_END();
}


struct pt pt_test_basic_2;


PT_THREAD(test_basic_2(process_event_t ev, process_data_t data, enum TestResult *pResult))
{
    TEST_BEGIN(pt_test_basic_2);

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);
    g_pb_2 = pubnub_get_ctx(1);
    pubnub_init(g_pb_2, pubkey, subkey);

    expect(PNR_STARTED == pubnub_subscribe(g_pb_2, channel));

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test 2\""));
    etimer_set(&g_et, 5*CLOCK_SECOND);

    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(data) == PNR_OK);

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(data) == PNR_OK);
    expect(got_messages(data, "\"Test 2\"", NULL));

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test 2 - 2\""));
    etimer_restart(&g_et);

    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(data) == PNR_OK);

    expect(PNR_STARTED == pubnub_subscribe(g_pb_2, channel));

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(data) == PNR_OK);
    expect(got_messages(data, "\"Test 2 - 2\"", NULL));

    TEST_END();
}
