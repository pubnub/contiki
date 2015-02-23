/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub.h"

#include "pubnub_ccore.h"

#include "contiki-net.h"
#include "lib/assert.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#if VERBOSE_DEBUG
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...) do { } while(0)
#endif

#if PUBNUB_USE_MDNS

#include "mdns.h"
#define pubnub_dns_init() mdns_init()
#define pubnub_dns_query mdns_query
#define pubnub_dns_lookup(name, pIPaddr) pIPaddr = mdns_lookup(name)
#define pubnub_dns_event mdns_event_found

#else

#define pubnub_dns_init() process_start(&resolv_process, NULL)
#define pubnub_dns_query resolv_query
#define pubnub_dns_lookup(name, pIPaddr) (resolv_lookup(PUBNUB_ORIGIN, &pIPaddr) == RESOLV_STATUS_CACHED) ? 0 : (pIPaddr = NULL)
#define pubnub_dns_event resolv_event_found

#endif


PROCESS(pubnub_process, "PubNub process");


process_event_t pubnub_publish_event;
process_event_t pubnub_subscribe_event;
process_event_t pubnub_leave_event;

#define HTTP_PORT 80


/** Type of Pubnub transaction */
enum pubnub_trans {
    /** No transaction at all */
    PBTT_NONE,
    /** Subscribe transaction */
    PBTT_SUBSCRIBE,
    /** Publish transaction */
    PBTT_PUBLISH,
    /** Leave (channel(s)) transaction */
    PBTT_LEAVE,
};

/** States of a context */
enum pubnub_state {
    PS_IDLE,
    PS_WAIT_DNS,
    PS_CONNECT,
    PS_TRANSACTION,
    PS_WAIT_CLOSE,
    PS_WAIT_CANCEL,
    PS_WAIT_CANCEL_CLOSE
};

/** The Pubnub context */
struct pubnub {
    struct pbcc_context core;

    /** Process that started last transaction */
    struct process *initiator;

    /* Network communication state */
    enum pubnub_state state;
    enum pubnub_trans trans;
    struct psock psock;

};

/** The PubNub contexts */
static struct pubnub m_aCtx[PUBNUB_CTX_MAX];


static bool valid_ctx_ptr(pubnub_t const *pb)
{
    return ((pb >= m_aCtx) && (pb < m_aCtx + PUBNUB_CTX_MAX));
}


/** Handles start of a TCP (HTTP) connection. It first handles DNS
    resolving for the context @p pb.  If DNS is already resolved, it
    proceeds to establishing TCP connection. Otherwise, will issue a
    DNS request and go to state of awaiting response.

    Call this function on start of a transaction or on receiving
    response from DNS server.

    @param pb The context for which to handle starting of connection
*/
static void handle_start_connect(pubnub_t *pb)
{
    uip_ipaddr_t *ipaddrptr;
    
    assert(valid_ctx_ptr(pb));
    assert((pb->state == PS_IDLE) || (pb->state == PS_WAIT_DNS));
    
    pubnub_dns_lookup(PUBNUB_ORIGIN, ipaddrptr);
    if (NULL == ipaddrptr) {
        DEBUG_PRINTF("Pubnub: DNS Querying for %s\n", PUBNUB_ORIGIN);
        pubnub_dns_query(PUBNUB_ORIGIN);
        pb->state = PS_WAIT_DNS;
        return;
    }
    
    PROCESS_CONTEXT_BEGIN(&pubnub_process);
    tcp_connect(ipaddrptr, uip_htons(HTTP_PORT), pb);
    PROCESS_CONTEXT_END(&pubnub_process);
    pb->state = PS_CONNECT;
}


void pubnub_init(pubnub_t *p, const char *publish_key, const char *subscribe_key)
{
    assert(valid_ctx_ptr(p));

    pbcc_init(&p->core, publish_key, subscribe_key);
    p->state = PS_IDLE;
    p->trans = PBTT_NONE;
}


void pubnub_done(pubnub_t *pb)
{
    assert(valid_ctx_ptr(pb));
    pubnub_cancel(pb);
}


static process_event_t trans2event(enum pubnub_trans trans)
{
    switch (trans) {
    case PBTT_SUBSCRIBE:
        return pubnub_subscribe_event;
    case PBTT_PUBLISH:
        return pubnub_publish_event;
    case PBTT_LEAVE:
        return pubnub_leave_event;
    case PBTT_NONE:
    default:
        assert(0);
        return PROCESS_EVENT_NONE;
    }
}


static void trans_outcome(pubnub_t *pb, enum pubnub_res result)
{
    pb->core.last_result = result;
    
    DEBUG_PRINTF("Pubnub: Transaction outcome: %d, HTTP code: %d\n",
                 result, pb->core.http_code
        );
    if ((result == PNR_FORMAT_ERROR) || (PUBNUB_MISSMSG_OK && (result != PNR_OK))) {
        /* In case of PubNub protocol error, abort an ongoing
         * subscribe and start over. This means some messages were
         * lost, but allows us to recover from bad situations,
         * e.g. too many messages queued or unexpected problem caused
         * by a particular message. */
        pb->core.timetoken[0] = '0';
        pb->core.timetoken[1] = '\0';
    }
    
    pb->state = PS_IDLE;
    process_post(pb->initiator, trans2event(pb->trans), pb);
}


void pubnub_cancel(pubnub_t *pb)
{
    assert(valid_ctx_ptr(pb));
    
    switch (pb->state) {
    case PS_WAIT_CANCEL:
    case PS_WAIT_CANCEL_CLOSE:
    case PS_IDLE:
        break;
    case PS_WAIT_DNS:
        pb->core.msg_ofs = pb->core.msg_end = 0;
        trans_outcome(pb, PNR_CANCELLED);
        break;
    default:
        pb->state = PS_WAIT_CANCEL;
        break;                        
    }
}


enum pubnub_res pubnub_publish(pubnub_t *pb, const char *channel, const char *message)
{
    enum pubnub_res rslt;

    assert(valid_ctx_ptr(pb));
    
    if (pb->state != PS_IDLE) {
        return PNR_IN_PROGRESS;
    }

    rslt = pbcc_publish_prep(&pb->core, channel, message);
    if (PNR_STARTED == rslt) {
        pb->initiator = PROCESS_CURRENT();
        pb->trans = PBTT_PUBLISH;
        handle_start_connect(pb);
    }
    
    return rslt;
}


char const *pubnub_get(pubnub_t *pb)
{
    assert(valid_ctx_ptr(pb));

    return pbcc_get_msg(&pb->core);
}


char const *pubnub_get_channel(pubnub_t *pb)
{
    assert(valid_ctx_ptr(pb));

    return pbcc_get_channel(&pb->core);
}


enum pubnub_res pubnub_subscribe(pubnub_t *p, const char *channel)
{
    enum pubnub_res rslt;

    assert(valid_ctx_ptr(p));
    
    if (p->state != PS_IDLE) {
        return PNR_IN_PROGRESS;
    }
    
    rslt = pbcc_subscribe_prep(&p->core, channel);
    if (PNR_STARTED == rslt) {
        p->initiator = PROCESS_CURRENT();
        p->trans = PBTT_SUBSCRIBE;
        handle_start_connect(p);
    }
    
    return rslt;
}


enum pubnub_res pubnub_leave(pubnub_t *p, const char *channel)
{
    enum pubnub_res rslt;

    assert(valid_ctx_ptr(p));
    
    if (p->state != PS_IDLE) {
        return PNR_IN_PROGRESS;
    }
    
    rslt = pbcc_leave_prep(&p->core, channel);
    if (PNR_STARTED == rslt) {
        p->initiator = PROCESS_CURRENT();
        p->trans = PBTT_LEAVE;
        handle_start_connect(p);
    }
    
    return rslt;
}


static void handle_dns_found(char const* name)
{
    pubnub_t *pb;
    
    DEBUG_PRINTF("Pubnub: DNS event '%s' - origin: '%s'\n", name, PUBNUB_ORIGIN);
    
    if (0 != strcmp(name, PUBNUB_ORIGIN)) {
        return;
    }
    for (pb = m_aCtx; pb != m_aCtx + PUBNUB_CTX_MAX; ++pb) {
        if (pb->state == PS_WAIT_DNS) {
            handle_start_connect(pb);
        }
    }
}


#define PSOCK_SEND_LITERAL_STR(psock, litstr) {       \
        uint8_t s_[] = litstr;                        \
        PSOCK_SEND((psock), s_, sizeof s_ - 1); }


PT_THREAD(handle_transaction(pubnub_t *pb))
{
    PSOCK_BEGIN(&pb->psock);
    
    pb->core.http_code = 0;
    
    /* Send HTTP request */
    DEBUG_PRINTF("Pubnub: Sending HTTP request...\n");
    PSOCK_SEND_LITERAL_STR(&pb->psock, "GET ");
    PSOCK_SEND_STR(&pb->psock, pb->core.http_buf);
    PSOCK_SEND_LITERAL_STR(&pb->psock, " HTTP/1.1\r\nHost: ");
    PSOCK_SEND_STR(&pb->psock, PUBNUB_ORIGIN);
    PSOCK_SEND_LITERAL_STR(&pb->psock, "\r\nUser-Agent: PubNub-ConTiki/0.1\r\nConnection: Keep-Alive\r\n\r\n");
    
    /* Read HTTP response status line */
    DEBUG_PRINTF("Pubnub: Reading HTTP response status line...\n");
    PSOCK_READTO(&pb->psock, '\n');
    if (strncmp(pb->core.http_buf, "HTTP/1.", 7) != 0) {
        trans_outcome(pb, PNR_IO_ERROR);
        PSOCK_CLOSE_EXIT(&pb->psock);
    }
    pb->core.http_code = atoi(pb->core.http_buf + 9);
    
    /* Read response header to find out either the length of the body
       or that body is chunked.
    */
    DEBUG_PRINTF("Pubnub: Reading HTTP response header...\n");
    pb->core.http_content_len = 0;
    pb->core.http_chunked = false;
    while (PSOCK_DATALEN(&pb->psock) > 2) {
        PSOCK_READTO(&pb->psock, '\n');
        char h_chunked[] = "Transfer-Encoding: chunked";
        char h_length[] = "Content-Length: ";
        if (strncmp(pb->core.http_buf, h_chunked, sizeof h_chunked - 1) == 0) {
            pb->core.http_chunked = true;
        }
        else if (strncmp(pb->core.http_buf, h_length, sizeof h_length - 1) == 0) {
            pb->core.http_content_len = atoi(pb->core.http_buf + sizeof h_length - 1);
            if (pb->core.http_content_len > PUBNUB_REPLY_MAXLEN) {
                trans_outcome(pb, PNR_IO_ERROR);
                PSOCK_CLOSE_EXIT(&pb->psock);
            }
        }
    }
    
    /* Read the body - either at once, or chunk by chunk */
    DEBUG_PRINTF("Pubnub: Reading HTTP response body...");
    pb->core.http_buf_len = 0;
    if (pb->core.http_chunked) {
        DEBUG_PRINTF("...chunked\n");
        for (;;) {
            PSOCK_READTO(&pb->psock, '\n');
            pb->core.http_content_len = strtoul(pb->core.http_buf, NULL, 16);
            if (pb->core.http_content_len == 0) {
                break;
            }
            if (pb->core.http_content_len > sizeof pb->core.http_buf) {
                trans_outcome(pb, PNR_IO_ERROR);
                PSOCK_CLOSE_EXIT(&pb->psock);
            }
            if (pb->core.http_buf_len + pb->core.http_content_len > PUBNUB_REPLY_MAXLEN) {
                trans_outcome(pb, PNR_IO_ERROR);
                PSOCK_CLOSE_EXIT(&pb->psock);
            }
            PSOCK_READBUF_LEN(&pb->psock, pb->core.http_content_len + 2);
            memcpy(
                pb->core.http_reply + pb->core.http_buf_len, 
                pb->core.http_buf, 
                pb->core.http_content_len
                );
            pb->core.http_buf_len += pb->core.http_content_len;
        }
    }
    else {
        DEBUG_PRINTF("...regular\n");
        while (pb->core.http_buf_len < pb->core.http_content_len) {
            PSOCK_READBUF_LEN(&pb->psock, pb->core.http_content_len - pb->core.http_buf_len);
            memcpy(
                pb->core.http_reply + pb->core.http_buf_len, 
                pb->core.http_buf, 
                PSOCK_DATALEN(&pb->psock)
                );
            pb->core.http_buf_len += PSOCK_DATALEN(&pb->psock);
        }
    }
    pb->core.http_reply[pb->core.http_buf_len] = '\0';
    
    DEBUG_PRINTF("Pubnub: done reading HTTP response\n");
    if (PBTT_SUBSCRIBE == pb->trans) {
        if (pbcc_parse_subscribe_response(&pb->core) != 0) {
            trans_outcome(pb, PNR_FORMAT_ERROR);
            PSOCK_CLOSE_EXIT(&pb->psock);
        }
    }
    
    PSOCK_CLOSE(&pb->psock);
    pb->state = PS_WAIT_CLOSE;
    
    PSOCK_END(&pb->psock);
}


static void handle_tcpip(pubnub_t *pb)
{
    if ((PS_IDLE == pb->state) || (PS_WAIT_DNS == pb->state)) {
        return;
    }
    if (uip_aborted()) {
        trans_outcome(pb, PNR_ABORTED);
        return;
    }
    else if (uip_timedout()) {
        trans_outcome(pb, PNR_TIMEOUT);
        return;
    }
    
    switch (pb->state) {
    case PS_CONNECT:
        if (uip_closed()) {
            trans_outcome(pb, PNR_IO_ERROR);
        }
        else if (uip_connected()) {
            PSOCK_INIT(&pb->psock, (uint8_t*)pb->core.http_buf, sizeof pb->core.http_buf);
            pb->state = PS_TRANSACTION;
            handle_transaction(pb);
        }
        break;
    case PS_TRANSACTION:
        if (uip_closed()) {
            trans_outcome(pb, PNR_IO_ERROR);
        }
        else {
            handle_transaction(pb);
        }
        break;
    case PS_WAIT_CLOSE:
        if (uip_closed()) {
            tcp_markconn(uip_conn, NULL);
            trans_outcome(pb, (pb->core.http_code / 100 == 2) ? PNR_OK : PNR_HTTP_ERROR);
        }
        break;
    case PS_WAIT_CANCEL:
        uip_close();
        pb->state = PS_WAIT_CANCEL_CLOSE;
        break;
    case PS_WAIT_CANCEL_CLOSE:
        if (uip_closed()) {
            tcp_markconn(uip_conn, NULL);
            pb->core.msg_ofs = pb->core.msg_end = 0;
            trans_outcome(pb, PNR_CANCELLED);
        }
        break;
    default:
        assert(0);
        break;
    }
}


PROCESS_THREAD(pubnub_process, ev, data)
{
    PROCESS_BEGIN();
    
    DEBUG_PRINTF("Pubnub: process started...\n");
    pubnub_publish_event = process_alloc_event();
    pubnub_subscribe_event = process_alloc_event();
    pubnub_leave_event = process_alloc_event();
    
    pubnub_dns_init();
    
    while (ev != PROCESS_EVENT_EXIT) {
        PROCESS_WAIT_EVENT();
        
        if (ev == tcpip_event) {
            if (data != NULL) {
                handle_tcpip(data);
            }            
        }
        else if (ev == pubnub_dns_event) {
            if (data != NULL) {
                handle_dns_found(data);
            }                
        }
    }
    
    PROCESS_END();
}


pubnub_t *pubnub_get_ctx(unsigned index)
{
    assert(index < PUBNUB_CTX_MAX);
    return m_aCtx + index;
}


void pubnub_set_uuid(pubnub_t *pb, const char *uuid)
{
    assert(valid_ctx_ptr(pb));
    pbcc_set_uuid(&pb->core, uuid);
}


void pubnub_set_auth(pubnub_t *pb, const char *auth)
{
    assert(valid_ctx_ptr(pb));
    pbcc_set_auth(&pb->core, auth);
}


enum pubnub_res pubnub_last_result(pubnub_t const *pb)
{
    assert(valid_ctx_ptr(pb));
    return pb->core.last_result;
}


int pubnub_last_http_code(pubnub_t const *pb)
{
    assert(valid_ctx_ptr(pb));
    return pb->core.http_code;
}
