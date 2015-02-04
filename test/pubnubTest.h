#if !defined INC_PUBNUBTEST
#define      INC_PUBNUBTEST

#include "pt.h"
#include "etimer.h"

#include "../pubnub.h"

#include <stdbool.h>
#include <stdio.h>


enum TestResult {
    trFail = -1,
    trIndeterminate,
    trPass
};


extern const char pubkey[];
extern const char subkey[];
extern const char channel[];

extern struct pt *g_current_pt;
extern struct pubnub *g_pb;
extern struct pubnub *g_pb_2;
extern struct etimer g_et;


#define TEST_BEGIN(pt) g_current_pt = &pt; PT_BEGIN(&pt)

#define TEST_DECL(tst) extern struct pt pt_test_##tst; PT_THREAD(test_##tst(process_event_t ev, process_data_t data, enum TestResult *pResult));

#define TEST_DEF(tst) struct pt pt_test_##tst; PT_THREAD(test_##tst(process_event_t ev, process_data_t data, enum TestResult *pResult)) { TEST_BEGIN(pt_test_##tst);

#define TEST_END() *pResult = trPass; PT_END(g_current_pt)
#define TEST_ENDDEF TEST_END(); }
#define TEST_YIELD() PT_YIELD(g_current_pt)

#define expect(exp) if (exp) {} else { printf("\x1b[31m expect(%s) failed, file %s function %s line %d\x1b[0m\n", #exp, __FILE__, __FUNCTION__, __LINE__); *pResult = trFail; PT_EXIT(g_current_pt); }
#define expect_ev(exp_ev) if (ev == exp_ev) {} else { printf("\x1b[31m Expected event %d (%s) but got %d (%s), file %s function %s line %d\x1b[0m\n", exp_ev, #exp_ev, ev, "", __FILE__, __FUNCTION__, __LINE__); *pResult = trFail; PT_EXIT(g_current_pt); }
#define expect_last_result(pbp, exp_rslt) { enum pubnub_res M_rslt_ = pubnub_last_result(pbp); if (M_rslt_ == exp_rslt) {} else if (M_rslt_ == PNR_ABORTED) { *pResult = trIndeterminate; PT_EXIT(g_current_pt); } else { printf("\x1b[31m Expected result %d (%s) but got %d (%s), file %s function %s line %d\x1b[0m\n", exp_rslt, #exp_rslt, M_rslt_, pubnub_res_2_string(M_rslt_), __FILE__, __FUNCTION__, __LINE__); *pResult = trFail; PT_EXIT(g_current_pt); } }
#define yield_expect_ev(exp_ev) TEST_YIELD(); expect_ev(exp_ev)

bool got_messages(pubnub_t *p, ...);

char const* pubnub_res_2_string(enum pubnub_res e);


#endif /* !defined INC_PUBNUBTEST */
