#include "pubnubTestBasic.h"

#include "../pubnub.h"


struct pt pt_test_medium_1;


PT_THREAD(test_medium_1(process_event_t ev, process_data_t data, enum TestResult *pResult))
{
    TEST_BEGIN(pt_test_medium_1);

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    pubnub_publish(g_pb, "ch,two", "\"Test M1\"");
    etimer_set(&g_et, 5*CLOCK_SECOND);

    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);

    pubnub_subscribe(g_pb, "ch");
    etimer_restart(&g_et);

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);
    expect(got_messages(g_pb, "\"Test M1\"", NULL));

    pubnub_subscribe(g_pb, "two");
    etimer_restart(&g_et);

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);
    expect(got_messages(g_pb, "\"Test M1\"", NULL));

    TEST_END();
}


struct pt pt_test_medium_2;


PT_THREAD(test_medium_2(process_event_t ev, process_data_t data, enum TestResult *pResult))
{
    TEST_BEGIN(pt_test_medium_2);

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    pubnub_publish(g_pb, "ch", "\"Test M2\"");
    etimer_set(&g_et, 5*CLOCK_SECOND);

    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);

    pubnub_publish(g_pb, "two", "\"Test M2 - 2\"");

    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);

    pubnub_subscribe(g_pb, "ch,two");
    etimer_restart(&g_et);

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(g_pb) == PNR_OK);
    expect(got_messages(g_pb, "\"Test M2\"", "\"Test M2 - 2\"", NULL));
    /* should probably check that M2 is on ch and M2 - 2 on two... */

    TEST_END();
}


struct pt pt_test_medium_3;


PT_THREAD(test_medium_3(process_event_t ev, process_data_t data, enum TestResult *pResult))
{
    TEST_BEGIN(pt_test_medium_3);

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);
    g_pb_2 = pubnub_get_ctx(1);
    pubnub_init(g_pb_2, pubkey, subkey);

    pubnub_publish(g_pb, "ch,two", "\"Test M3\"");
    pubnub_publish(g_pb_2, "tree", "\"Test M3 - 1\"");
    etimer_set(&g_et, 5*CLOCK_SECOND);

    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(data) == PNR_OK);

    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(data) == PNR_OK);

    expect(PNR_STARTED == pubnub_subscribe(g_pb, "two,tree"));
    expect(PNR_STARTED == pubnub_subscribe(g_pb_2, "ch"));
    etimer_restart(&g_et);

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(data) == PNR_OK);
    expect(got_messages(data, "\"Test M3\"", "\"Test M3 - 1\"", NULL));
    /* should probably check that M3 is on two (or tree) and M3 - 1 on ch. */

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(data) == PNR_OK);
    expect(got_messages(data, "\"Test M3\"", NULL));

    /* maybe this assumption that first subscribe will "land" the
       first subscribe event is not valid
    */

    TEST_END();
}


struct pt pt_test_medium_4;


PT_THREAD(test_medium_4(process_event_t ev, process_data_t data, enum TestResult *pResult))
{
    TEST_BEGIN(pt_test_medium_4);

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test M4\""));
    pubnub_cancel(g_pb);
    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test M4 - 2\""));
    etimer_set(&g_et, 5*CLOCK_SECOND);

    TEST_YIELD();

    expect(ev == pubnub_publish_event);
    expect(pubnub_last_result(data) == PNR_OK);

    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));
    etimer_restart(&g_et);

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(data) == PNR_OK);
    expect(got_messages(data, "\"Test M4 - 2\"", NULL));

    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));
    pubnub_cancel(g_pb);
    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test M4 - 3\""));
    expect(PNR_STARTED == pubnub_subscribe(g_pb, channel));

    TEST_YIELD();

    expect(ev == pubnub_subscribe_event);
    expect(pubnub_last_result(data) == PNR_OK);
    expect(got_messages(data, "\"Test M4 - 3\"", NULL));

    TEST_END();
}


struct pt pt_test_medium_wrong_api;


PT_THREAD(test_medium_wrong_api(process_event_t ev, process_data_t data, enum TestResult *pResult))
{
    TEST_BEGIN(pt_test_medium_wrong_api);

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    expect(PNR_STARTED == pubnub_publish(g_pb, channel, "\"Test \""));
    expect(PNR_IN_PROGRESS == pubnub_publish(g_pb, channel, "\"Test - 2\""));
    expect(PNR_IN_PROGRESS == pubnub_subscribe(g_pb, channel));
    pubnub_cancel(g_pb);
    expect(PNR_IN_PROGRESS == pubnub_subscribe(g_pb, channel));
    expect(PNR_IN_PROGRESS == pubnub_publish(g_pb, channel, "\"Test - 3\""));
    expect(PNR_IN_PROGRESS == pubnub_subscribe(g_pb, channel));

    TEST_END();
}


struct pt pt_test_medium_cloud_err;


PT_THREAD(test_medium_cloud_err(process_event_t ev, process_data_t data, enum TestResult *pResult))
{
    TEST_BEGIN(pt_test_medium_cloud_err);

    g_pb = pubnub_get_ctx(0);
    pubnub_init(g_pb, pubkey, subkey);

    expect(PNR_IN_PROGRESS == pubnub_subscribe(g_pb, ","));
    etimer_set(&g_et, 30*CLOCK_SECOND);

    TEST_YIELD();

    expect(PROCESS_EVENT_TIMER == ev);

    TEST_END();
}


