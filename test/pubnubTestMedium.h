#if !defined INC_PUBNUBTESTMEDIUM
#define      INC_PUBNUBTESTMEDIUM


#include "pubnubTest.h"


extern struct pt pt_test_medium_1;

PT_THREAD(test_medium_1(process_event_t ev, process_data_t data, enum TestResult *pResult));


extern struct pt pt_test_medium_2;

PT_THREAD(test_medium_2(process_event_t ev, process_data_t data, enum TestResult *pResult));


extern struct pt pt_test_medium_3;

PT_THREAD(test_medium_3(process_event_t ev, process_data_t data, enum TestResult *pResult));


extern struct pt pt_test_medium_4;

PT_THREAD(test_medium_4(process_event_t ev, process_data_t data, enum TestResult *pResult));


extern struct pt pt_test_medium_wrong_api;

PT_THREAD(test_medium_wrong_api(process_event_t ev, process_data_t data, enum TestResult *pResult));


extern struct pt pt_test_medium_cloud_err;

PT_THREAD(test_medium_cloud_err(process_event_t ev, process_data_t data, enum TestResult *pResult));



#endif /* !defined INC_PUBNUBTESTMEDIUM */
