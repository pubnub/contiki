#if !defined INC_PUBNUBTESTBASIC
#define      INC_PUBNUBTESTBASIC


#include "pubnubTest.h"


extern struct pt pt_test_basic_1;

PT_THREAD(test_basic_1(process_event_t ev, process_data_t data, enum TestResult *pResult));

extern struct pt pt_test_basic_2;

PT_THREAD(test_basic_2(process_event_t ev, process_data_t data, enum TestResult *pResult));


#endif /* !defined INC_PUBNUBTESTBASIC */
