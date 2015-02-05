/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */


#include "../pubnub.h"

#include "pubnubTest.h"

#include <string.h>
#include <stdarg.h>

/** Helper function to translate Pubnub result to a string */
char const* pubnub_res_2_string(enum pubnub_res e)
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

    return !missing;
}
