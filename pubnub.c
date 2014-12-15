/* -*- c-file-style:"stroustrup" -*- */
#include "pubnub.h"

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

/* !!! Ovo verovatno valya izbaciti */
enum pubnub_state {
    PS_IDLE,
    PS_WAIT_DNS,
    PS_CONNECT,
    PS_TRANSACTION
};

/** The Pubnub context */
struct pubnub {
    /* Configuration and global state. */
    const char *publish_key, *subscribe_key;
    const char *uuid, *auth;
    char timetoken[64];

    /** Process that started last transaction */
    struct process *initiator;

    /** The result of the last Pubnub transaction */
    enum pubnub_res last_result;

    /* Network communication state */
    enum pubnub_state state;
    enum pubnub_trans trans;
    struct psock psock;
    union { char url[PUBNUB_BUF_MAXLEN]; char line[PUBNUB_BUF_MAXLEN]; } http_buf;
    int http_code;
    unsigned http_buf_len;
    unsigned http_content_len;
    bool http_chunked;
    char http_reply[PUBNUB_REPLY_MAXLEN];

    /* These in-string offsets are used for yielding messages received
     * by subscribe - the beginning of last yielded message and total
     * length of message buffer, and the same for channels.
     */
    unsigned short msg_ofs, msg_end, chan_ofs, chan_end;

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

    if (resolv_lookup(PUBNUB_ORIGIN, &ipaddrptr) != RESOLV_STATUS_CACHED) {
	DEBUG_PRINTF("Pubnub: Querying for %s\n", PUBNUB_ORIGIN);
	resolv_query(PUBNUB_ORIGIN);
	pb->state = PS_WAIT_DNS;
	return;
    }
    DEBUG_PRINTF("Pubnub: '%s' resolved to: %d.%d.%d.%d\n", PUBNUB_ORIGIN
		 , ipaddrptr->u8[0]
		 , ipaddrptr->u8[1]
		 , ipaddrptr->u8[2]
		 , ipaddrptr->u8[3]
	);
    PROCESS_CONTEXT_BEGIN(&pubnub_process);
    tcp_connect(ipaddrptr, uip_htons(HTTP_PORT), pb);
    PROCESS_CONTEXT_END(&pubnub_process);
    pb->state = PS_CONNECT;
}


/* Find the beginning of a JSON string that comes after comma and ends
 * at @c &buf[len].
 * @return position (index) of the found start or -1 on error. */
static int find_string_start(char const *buf, int len)
{
    int i;
    for (i = len-1; i > 0; i--) {
        if (buf[i] == '"') {
            return (buf[i-1] == ',') ? i : -1;
	}
    }
    return -1;
}


/** Split @p buf string containing a JSON array (with arbitrary
 * contents) to multiple NUL-terminated C strings, in-place.
 */
static bool split_array(char *buf)
{
    bool escaped = false;
    bool in_string = false;
    int bracket_level = 0;

    for (; *buf != '\0'; ++buf) {
        if (escaped) {
            escaped = false;
        } 
	else if ('"' == *buf) {
	    in_string = !in_string;
	}
	else if (in_string) {
	    escaped = ('\\' == *buf);
        }
	else {
            switch (*buf) {
	    case '[': case '{': bracket_level++; break;
	    case ']': case '}': bracket_level--; break;
                /* if at root, split! */
	    case ',': if (bracket_level == 0) *buf = '\0'; break;
	    default: break;
            }
        }
    }

    return !(escaped || in_string || (bracket_level > 0));
}


void pubnub_init(pubnub_t *p, const char *publish_key, const char *subscribe_key)
{
    assert(valid_ctx_ptr(p));

    p->publish_key = publish_key;
    p->subscribe_key = subscribe_key;
    p->timetoken[0] = '0';
    p->timetoken[1] = '\0';
    p->uuid = p->auth = NULL;
    p->state = PS_IDLE;
    p->trans = PBTT_NONE;
    p->msg_ofs = p->msg_end = 0;
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
    pb->last_result = result;

    DEBUG_PRINTF("Pubnub: Transaction outcome: %d, HTTP code: %d\n",
		 result, pb->http_code
	);
    if ((result == PNR_FORMAT_ERROR) || (PUBNUB_MISSMSG_OK && (result != PNR_OK))) {
	/* In case of PubNub protocol error, abort an ongoing
	 * subscribe and start over. This means some messages were
	 * lost, but allows us to recover from bad situations,
	 * e.g. too many messages queued or unexpected problem caused
	 * by a particular message. */
	pb->timetoken[0] = '0';
	pb->timetoken[1] = '\0';
    }

    pb->state = PS_IDLE;
    process_post(pb->initiator, trans2event(pb->trans), pb);
}


void pubnub_cancel(pubnub_t *pb)
{
    assert(valid_ctx_ptr(pb));

    if (pb->state != PS_IDLE) {
	trans_outcome(pb, PNR_CANCELLED);
    }
}


enum pubnub_res pubnub_publish(pubnub_t *pb, const char *channel, const char *message)
{
    assert(valid_ctx_ptr(pb));

    if (pb->state != PS_IDLE) {
        return PNR_IN_PROGRESS;
    }
    pb->initiator = PROCESS_CURRENT();
    pb->trans = PBTT_PUBLISH;
    pb->http_content_len = 0;

    pb->http_buf_len = snprintf(
	pb->http_buf.url, sizeof pb->http_buf.url,
	"/publish/%s/%s/0/%s/0/", 
	pb->publish_key, pb->subscribe_key, channel
	);

    const char *pmessage = message;
    while (pmessage[0]) {
        /* RFC 3986 Unreserved characters plus few
         * safe reserved ones. */
        size_t okspan = strspn(pmessage, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~" ",=:;@[]");
        if (okspan > 0) {
            if (okspan > sizeof(pb->http_buf.url)-1 - pb->http_buf_len) {
                pb->http_buf_len = 0;
		return PNR_TX_BUFF_TOO_SMALL;
            }
            memcpy(pb->http_buf.url + pb->http_buf_len, pmessage, okspan);
            pb->http_buf_len += okspan;
            pb->http_buf.url[pb->http_buf_len] = 0;
            pmessage += okspan;
        }
        if (pmessage[0]) {
            /* %-encode a non-ok character. */
            char enc[4] = {'%'};
            enc[1] = "0123456789ABCDEF"[pmessage[0] / 16];
            enc[2] = "0123456789ABCDEF"[pmessage[0] % 16];
            if (3 > sizeof pb->http_buf.url - 1 - pb->http_buf_len) {
                pb->http_buf_len = 0;
		return PNR_TX_BUFF_TOO_SMALL;
            }
            memcpy(pb->http_buf.url + pb->http_buf_len, enc, 4);
            pb->http_buf_len += 3;
            ++pmessage;
        }
    }

    handle_start_connect(pb);

    return PNR_STARTED;
}


static int parse_subscribe_response(pubnub_t *p)
{
    char *reply = p->http_reply;
    int replylen = strlen(reply);
    if (reply[replylen-1] != ']' && replylen > 2) {
        replylen -= 2; // XXX: this seems required by Manxiang
    }
    if ((reply[0] != '[') || (reply[replylen-1] != ']') || (reply[replylen-2] != '"')) {
        return -1;
    }

    /* Extract the last argument. */
    int i = find_string_start(reply, replylen-2);
    if (i < 0) {
        return -1;
    }
    reply[replylen - 2] = 0;

    /* Now, the last argument may either be a timetoken or a channel list. */
    if (reply[i-2] == '"') {
        int k;
        /* It is a channel list, there is another string argument in front
         * of us. Process the channel list ... */
        p->chan_ofs = i+1;
        p->chan_end = replylen - 1;
        for (k = p->chan_end - 1; k > p->chan_ofs; --k) {
            if (reply[k] == ',') {
                reply[k] = 0;
	    }
	}

        /* ... and look for timetoken again. */
	reply[i-2] = 0;
        i = find_string_start(reply, i-2);
        if (i < 0) {
            return -1;
        }
    } 
    else {
        p->chan_ofs = 0;
        p->chan_end = 0;
    }

    /* Now, i points at
     * [[1,2,3],"5678"]
     * [[1,2,3],"5678","a,b,c"]
     *          ^-- here */

    /* Setup timetoken. */
    if (replylen-2 - (i+1) >= sizeof p->timetoken) {
        return -1;
    }
    strcpy(p->timetoken, reply + i+1);
    reply[i-2] = 0; // terminate the [] message array (before the ]!)

    /* Set up the message list - offset, length and NUL-characters splitting
     * the messages. */
    p->msg_ofs = 2;
    p->msg_end = i-2;

    return split_array(reply + p->msg_ofs) ? 0 : -1;
}


char const *pubnub_get(pubnub_t *pb)
{
    assert(valid_ctx_ptr(pb));

    if (pb->msg_ofs < pb->msg_end) {
	char const *rslt = pb->http_reply + pb->msg_ofs;
	pb->msg_ofs += strlen(rslt);
	if (pb->msg_ofs++ <= pb->msg_end) {
	    return rslt;
	}
    }

    return NULL;
}


char const *pubnub_get_channel(pubnub_t *pb)
{
    assert(valid_ctx_ptr(pb));

    if (pb->chan_ofs < pb->chan_end) {
	char const* rslt = pb->http_reply + pb->chan_ofs;
	pb->chan_ofs += strlen(rslt);
	if (pb->chan_ofs++ <= pb->chan_end) {
	    return rslt;
	}
    }

    return NULL;
}


enum pubnub_res pubnub_subscribe(pubnub_t *p, const char *channel)
{
    assert(valid_ctx_ptr(p));

    if (p->state != PS_IDLE) {
        return PNR_IN_PROGRESS;
    }
    if (p->msg_ofs < p->msg_end) {
	return PNR_RX_BUFF_NOT_EMPTY;
    }

    p->initiator = PROCESS_CURRENT();
    p->trans = PBTT_SUBSCRIBE;
    p->http_content_len = 0;
    p->msg_ofs = 0;

    p->http_buf_len = snprintf(p->http_buf.url, sizeof(p->http_buf.url),
            "/subscribe/%s/%s/0/%s?" "%s%s" "%s%s%s" "&pnsdk=PubNub-Contiki-%s%%2F%s",
            p->subscribe_key, channel, p->timetoken,
            p->uuid ? "uuid=" : "", p->uuid ? p->uuid : "",
            p->uuid && p->auth ? "&" : "",
            p->auth ? "auth=" : "", p->auth ? p->auth : "",
            "", "1.1"
            );

    handle_start_connect(p);

    return PNR_STARTED;
}


enum pubnub_res pubnub_leave(pubnub_t *p, const char *channel)
{
    assert(valid_ctx_ptr(p));

    if (p->state != PS_IDLE) {
        return PNR_IN_PROGRESS;
    }

    p->initiator = PROCESS_CURRENT();
    p->trans = PBTT_LEAVE;
    p->http_content_len = 0;

    /* Make sure next subscribe() will be a join. */
    p->timetoken[0] = '0';
    p->timetoken[1] = '\0';

    p->http_buf_len = snprintf(p->http_buf.url, sizeof(p->http_buf.url),
            "/v2/presence/sub-key/%s/channel/%s/leave?" "%s%s" "%s%s%s",
	    p->subscribe_key, 
	    channel,
            p->uuid ? "uuid=" : "", p->uuid ? p->uuid : "",
            p->uuid && p->auth ? "&" : "",
            p->auth ? "auth=" : "", p->auth ? p->auth : "");

    handle_start_connect(p);
    return PNR_STARTED;
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


#define PSOCK_SEND_LITERAL_STR(psock, litstr) { \
	uint8_t s_[] = litstr;			\
	PSOCK_SEND((psock), s_, sizeof s_ - 1); }


PT_THREAD(handle_transaction(pubnub_t *pb))
{
    PSOCK_BEGIN(&pb->psock);

    pb->http_code = 0;

    /* Send HTTP request */
    DEBUG_PRINTF("Pubnub: Sending HTTP request...\n");
    PSOCK_SEND_LITERAL_STR(&pb->psock, "GET ");
    PSOCK_SEND_STR(&pb->psock, pb->http_buf.url);
    PSOCK_SEND_LITERAL_STR(&pb->psock, " HTTP/1.1\r\nHost: ");
    PSOCK_SEND_STR(&pb->psock, PUBNUB_ORIGIN);
    PSOCK_SEND_LITERAL_STR(&pb->psock, "\r\nUser-Agent: PubNub-ConTiki/0.1\r\nConnection: Keep-Alive\r\n\r\n");
    DEBUG_PRINTF("Pubnub: Sent.\n");

    /* Read HTTP response status line */
    DEBUG_PRINTF("Pubnub: Reading HTTP response status line...\n");
    PSOCK_READTO(&pb->psock, '\n');
    if (strncmp(pb->http_buf.line, "HTTP/1.", 7) != 0) {
	trans_outcome(pb, PNR_IO_ERROR);
	PSOCK_CLOSE_EXIT(&pb->psock);
    }
    pb->http_code = atoi(pb->http_buf.line + 9);

    /* Read response header to find out either the length of the body
       or that body is chunked.
    */
    DEBUG_PRINTF("Pubnub: Reading HTTP response header...\n");
    pb->http_content_len = 0;
    pb->http_chunked = false;
    while (PSOCK_DATALEN(&pb->psock) > 2) {
	PSOCK_READTO(&pb->psock, '\n');
	char h_chunked[] = "Transfer-Encoding: chunked";
	char h_length[] = "Content-Length: ";
	if (strncmp(pb->http_buf.line, h_chunked, sizeof h_chunked - 1) == 0) {
	    pb->http_chunked = true;
	}
	else if (strncmp(pb->http_buf.line, h_length, sizeof h_length - 1) == 0) {
	    pb->http_content_len = atoi(pb->http_buf.line + sizeof h_length - 1);
	    if (pb->http_content_len > PUBNUB_REPLY_MAXLEN) {
		trans_outcome(pb, PNR_IO_ERROR);
		PSOCK_CLOSE_EXIT(&pb->psock);
	    }
	}
    }

    /* Read the body - either at once, or chunk by chunk */
    DEBUG_PRINTF("Pubnub: Reading HTTP response body...");
    pb->http_buf_len = 0;
    if (pb->http_chunked) {
	DEBUG_PRINTF("...chunked\n");
	for (;;) {
	    PSOCK_READTO(&pb->psock, '\n');
	    pb->http_content_len = strtoul(pb->http_buf.line, NULL, 16);
	    if (pb->http_content_len == 0) {
		break;
	    }
	    if (pb->http_content_len > sizeof pb->http_buf.line) {
		trans_outcome(pb, PNR_IO_ERROR);
		PSOCK_CLOSE_EXIT(&pb->psock);
	    }
	    if (pb->http_buf_len + pb->http_content_len > PUBNUB_REPLY_MAXLEN) {
		trans_outcome(pb, PNR_IO_ERROR);
		PSOCK_CLOSE_EXIT(&pb->psock);
	    }
	    PSOCK_READBUF_LEN(&pb->psock, pb->http_content_len + 2);
	    memcpy(
		pb->http_reply + pb->http_buf_len, 
		pb->http_buf.line, 
		pb->http_content_len
		);
	    pb->http_buf_len += pb->http_content_len;
	}
    }
    else {
	DEBUG_PRINTF("...regular\n");
	while (pb->http_buf_len < pb->http_content_len) {
	    PSOCK_READBUF_LEN(&pb->psock, pb->http_content_len - pb->http_buf_len);
	    memcpy(
		pb->http_reply + pb->http_buf_len, 
		pb->http_buf.line, 
		PSOCK_DATALEN(&pb->psock)
		);
	    pb->http_buf_len += PSOCK_DATALEN(&pb->psock);
	}
    }
    pb->http_reply[pb->http_buf_len] = '\0';

    /* Notify the initiator that we're done */
    DEBUG_PRINTF("Pubnub: done reading HTTP response\n");
    if (PBTT_SUBSCRIBE == pb->trans) {
	if (parse_subscribe_response(pb) != 0) {
	    trans_outcome(pb, PNR_FORMAT_ERROR);
	    PSOCK_CLOSE_EXIT(&pb->psock);
	}
    }
    trans_outcome(pb, (pb->http_code / 100 == 2) ? PNR_OK : PNR_HTTP_ERROR);

    PSOCK_CLOSE(&pb->psock);

    PSOCK_END(&pb->psock);
}


static void handle_tcpip(pubnub_t *pb)
{
    switch (pb->state) {
    case PS_IDLE:
    case PS_WAIT_DNS:
	break;
    case PS_CONNECT:
	if (uip_aborted() || uip_closed()) {
	    return trans_outcome(pb, PNR_IO_ERROR);
	}
	else if (uip_timedout()) {
	    return trans_outcome(pb, PNR_TIMEOUT);
	}
	else if (uip_connected()) {
	    PSOCK_INIT(&pb->psock, (uint8_t*)pb->http_buf.line, sizeof pb->http_buf.line);
	    pb->state = PS_TRANSACTION;
	    handle_transaction(pb);
	}
	break;
    case PS_TRANSACTION:
	if (uip_aborted() || uip_closed()) {
	    return trans_outcome(pb, PNR_IO_ERROR);
	}
	else if (uip_timedout()) {
	    return trans_outcome(pb, PNR_TIMEOUT);
	}
	else {
	    handle_transaction(pb);
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

    process_start(&resolv_process, NULL);
	
    while (ev != PROCESS_EVENT_EXIT) {
	PROCESS_WAIT_EVENT();

	if (ev == tcpip_event) {
	    handle_tcpip(data);
	}
	else if (ev == resolv_event_found) {
	    handle_dns_found(data);
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
    pb->uuid = uuid;
}


void pubnub_set_auth(pubnub_t *pb, const char *auth)
{
    assert(valid_ctx_ptr(pb));
    pb->auth = auth;
}


enum pubnub_res pubnub_last_result(pubnub_t const *pb)
{
    assert(valid_ctx_ptr(pb));
    return pb->last_result;
}


int pubnub_last_http_code(pubnub_t const *pb)
{
    assert(valid_ctx_ptr(pb));
    return pb->http_code;
}
