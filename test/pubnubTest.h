#if !defined INC_PUBNUBTEST
#define      INC_PUBNUBTEST

#include "pt.h"
#include "etimer.h"

#include <stdbool.h>


enum TestResult {
    trFail = -1,
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
#define TEST_END() *pResult = trPass; PT_END(g_current_pt)
#define TEST_YIELD() PT_YIELD(g_current_pt)

#define expect(exp) if (exp) {} else { *pResult = trFail; PT_EXIT(g_current_pt); }

typedef struct pubnub pubnub_t;

bool got_messages(pubnub_t *p, ...);


#endif /* !defined INC_PUBNUBTEST */
