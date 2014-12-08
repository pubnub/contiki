/* -*- c-file-style:"stroustrup" -*- */
#include "cgreen/cgreen.h"
#include "cgreen/mocks.h"

#include "pubnub.h"

#include "contiki-net.h"

#include <stdlib.h>
#include <string.h>

/* A less chatty cgreen :) */

#define attest assert_that
#define equals is_equal_to
#define streqs is_equal_to_string
#define differs is_not_equal_to
#define strdifs is_not_equal_to_string
#define ptreqs(val) is_equal_to_contents_of(&(val), sizeof(val))
#define ptrdifs(val) is_not_equal_to_contents_of(&(val), sizeof(val))
#define sets(par, val) will_set_contents_of_parameter(par, &(val), sizeof(val))
#define sets_ex will_set_contents_of_parameter
#define returns will_return


/* Contiki test harness (for our purposes) */

#if PROCESS_CONF_NO_PROCESS_NAMES
#define STUB_PROCESS(name, strname) struct process name = { NULL, NULL }
#else
#define STUB_PROCESS(name, strname) struct process name = { NULL, strname, NULL }
#endif

enum {
    RESOLV_EVENT_FOUND = PROCESS_EVENT_MAX,
    TCPIP_EVENT,
    MAX_STATIC_EVENT_ALLOC
};
process_event_t resolv_event_found = RESOLV_EVENT_FOUND;
process_event_t tcpip_event = TCPIP_EVENT;

/* Global vars from Contiki, don't need to be initialized, but should
   get the right value in tests that require them.
*/
uint8_t uip_flags;

struct process *process_current;


STUB_PROCESS(resolv_process, "DNS resolver");


PT_THREAD(psock_send(struct psock *psock, const uint8_t *buf, unsigned int len))
{
    return (char)mock(psock, buf, len);
}

struct uip_conn *tcp_connect(uip_ipaddr_t *ripaddr, uint16_t port, void *appstate)
{
    return (struct uip_conn*)mock(ripaddr, port, appstate);
}


void resolv_query(char const *name)
{
    mock(name);
}

resolv_status_t resolv_lookup(const char *name, uip_ipaddr_t ** ipaddr)
{
    return (resolv_status_t)mock(name, ipaddr);
}



int process_post(struct process *p, process_event_t ev, void* data)
{
    return (int)mock(p, ev, data);
}


void process_start(struct process *p, const char *arg)
{
    mock(p, arg);
}


void _xassert(const char *s, int i)
{
    mock(s, i);
}


/* Not mocked, but copied to avoid linking whole modules which would
   require other modules, which would require other modules, which
   would... you get the picture.
*/

uint16_t uip_htons(uint16_t val) { return UIP_HTONS(val); }

uint16_t psock_datalen(struct psock *p) { return p->bufsize - p->buf.left; }


/* Like previous functions, these are not (just) mocked, but not copied
   either. They're implemented differently, as per our needs.
*/

process_event_t process_alloc_event(void)
{
    static process_event_t lastevent = MAX_STATIC_EVENT_ALLOC;
    return lastevent++;
}


static uint8_t *m_readbuf;
static size_t m_readbuf_size;
static uint8_t *m_readbuf_pos;
inline size_t readbuf_left() {
    return m_readbuf_size ? m_readbuf_size - (m_readbuf_pos - m_readbuf) : 0;
}

PT_THREAD(psock_readto(struct psock *psock, unsigned char c))
{
    if (readbuf_left() > 0) {
	uint8_t *next_pos = memchr(m_readbuf_pos, c, readbuf_left());
	if (NULL == next_pos) {
	    return PT_YIELDED;
	}
	++next_pos;
	size_t len = next_pos - m_readbuf_pos;
	memcpy(psock->bufptr, m_readbuf_pos, len);
	m_readbuf_pos = next_pos;
	psock->buf.left = psock->bufsize - len;

	return PT_ENDED;
    }

    return PT_YIELDED;
}


PT_THREAD(psock_readbuf_len(struct psock *psock, uint16_t len))
{
    attest(len, is_less_than(psock->bufsize));
    if (readbuf_left() >= len) {
	memcpy(psock->bufptr, m_readbuf_pos, len);
	m_readbuf_pos += len;
	psock->buf.left = psock->bufsize - len;
	return PT_ENDED;
    }
    return PT_YIELDED;
}


void psock_init(struct psock *psock, uint8_t *buffer, unsigned int buffersize)
{
    psock->bufptr = buffer;
    psock->bufsize = buffersize;
    PT_INIT(&psock->pt);
    PT_INIT(&psock->psockpt);

    mock(psock, buffer, buffersize);
}


/* ---------- TESTS ---------- */


Ensure(can_get_context) {
    size_t i;
    for (i = 0; i < PUBNUB_CTX_MAX; ++i) {
	attest(pubnub_get_ctx(i), differs(NULL));
    }
    expect(_xassert, when(s, streqs("pubnub.c")));
    pubnub_get_ctx(PUBNUB_CTX_MAX);
    expect(_xassert, when(s, streqs("pubnub.c")));
    pubnub_get_ctx(-1);
}


#define HTTP_PORT 80


Describe(single_context_pubnub);

static pubnub_t *pbp;
static uip_ipaddr_t pubnub_ip_addr;
static uip_ipaddr_t* pubnub_ip_addr_ptr = &pubnub_ip_addr;

BeforeEach(single_context_pubnub) {
    pbp = pubnub_get_ctx(0);
    attest(pbp, differs(NULL));

    expect(process_start, when(p, equals(&resolv_process)));
    attest(pubnub_process.thread(&pubnub_process.pt, PROCESS_EVENT_INIT, NULL), equals(PT_YIELDED));

    process_current = &pubnub_process;
}

AfterEach(single_context_pubnub) {
    pubnub_done(pbp);
    attest(pubnub_process.thread(&pubnub_process.pt, PROCESS_EVENT_EXIT, NULL), equals(PT_ENDED));
}


inline void incoming(char const*str) {
    m_readbuf = m_readbuf_pos = (uint8_t*)str;
    m_readbuf_size = strlen(str);
    attest(pubnub_process.thread(&pubnub_process.pt, TCPIP_EVENT, pbp), equals(PT_YIELDED));
}


void expect_cached_dns_for_pubnub_origin()
{
    expect(resolv_lookup, when(name, streqs(PUBNUB_ORIGIN)),
	   sets(ipaddr, pubnub_ip_addr_ptr),
	   returns(RESOLV_STATUS_CACHED));
    expect(tcp_connect,
	   when(ripaddr, equals(pubnub_ip_addr_ptr)),
	   when(port, equals(uip_htons(HTTP_PORT))),
	   when(appstate, equals(pbp)));
}


#define expect_event(ev_)				\
    expect(process_post,				\
	   when(p, equals(&pubnub_process)),		\
	   when(ev, equals(ev_)),			\
	   when(data, equals(pbp))			\
	)


inline void expect_outgoing_with_url(char const *url) {
    expect(psock_init, when(buffersize, is_less_than(128)));
    expect(psock_send, when(buf, streqs("GET ")), returns(PT_ENDED));
    expect(psock_send, when(buf, streqs(url)), returns(PT_ENDED));
    expect(psock_send, when(buf, streqs(" HTTP/1.1\r\nHost: ")), returns(PT_ENDED));
    expect(psock_send, when(buf, streqs(PUBNUB_ORIGIN)), returns(PT_ENDED));
    expect(psock_send, when(buf, streqs("\r\nUser-Agent: PubNub-ConTiki/0.1\r\nConnection: Keep-Alive\r\n\r\n")), returns(PT_ENDED));
}


Ensure(single_context_pubnub, leave_cached_dns) {
    pubnub_init(pbp, "pubkey", "subkey");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/v2/presence/sub-key/subkey/channel/lamanche/leave?");
    expect_event(pubnub_leave_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 2\r\n\r\n[]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
}


Ensure(single_context_pubnub, leave_query_dns) {
    pubnub_init(pbp, "pubkey", "subskey");

    expect(resolv_lookup, when(name, streqs(PUBNUB_ORIGIN)),
	   returns(RESOLV_STATUS_EXPIRED));
    expect(resolv_query, when(name, streqs(PUBNUB_ORIGIN)));
    attest(pubnub_leave(pbp, "dunav-tisa-dunav"), equals(PNR_STARTED));

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_process.thread(&pubnub_process.pt, RESOLV_EVENT_FOUND, PUBNUB_ORIGIN), equals(PT_YIELDED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/v2/presence/sub-key/subskey/channel/dunav-tisa-dunav/leave?");
    expect_event(pubnub_leave_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 2\r\n\r\n[]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
}


Ensure(single_context_pubnub, leave_cached_dns_uuid_auth) {
    pubnub_init(pbp, "pubkey", "subkey");

    /* Set UID */
    pubnub_set_uuid(pbp, "BABA-DEDA-DECA");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/v2/presence/sub-key/subkey/channel/lamanche/leave?uuid=BABA-DEDA-DECA");
    expect_event(pubnub_leave_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 2\r\n\r\n[]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));

    /* Set auth, too */
    pubnub_set_auth(pbp, "super-secret-key");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/v2/presence/sub-key/subkey/channel/lamanche/leave?uuid=BABA-DEDA-DECA&auth=super-secret-key");
    expect_event(pubnub_leave_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 2\r\n\r\n[]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));

    /* Reset UUID */
    pubnub_set_uuid(pbp, NULL);

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/v2/presence/sub-key/subkey/channel/lamanche/leave?auth=super-secret-key");
    expect_event(pubnub_leave_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 2\r\n\r\n[]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));

    /* Reset auth, too */
    pubnub_set_auth(pbp, NULL);

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/v2/presence/sub-key/subkey/channel/lamanche/leave?");

    expect_event(pubnub_leave_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 2\r\n\r\n[]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
}


Ensure(single_context_pubnub, leave_cached_dns_cancel) {
    pubnub_init(pbp, "pubkey", "subkey");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_STARTED));

    expect_event(pubnub_leave_event);
    pubnub_cancel(pbp);
    attest(pubnub_last_result(pbp), equals(PNR_CANCELLED));
}


Ensure(single_context_pubnub, leave_while_busy_fails) {
    pubnub_init(pbp, "pubkey", "subkey");

    /* Leave while leaving */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_STARTED));

    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_IN_PROGRESS));

    expect_event(pubnub_leave_event);
    pubnub_cancel(pbp);
    attest(pubnub_last_result(pbp), equals(PNR_CANCELLED));
}


Ensure(single_context_pubnub, publish_cached_dns) {
    pubnub_init(pbp, "publkey", "subkey");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_publish(pbp, "jarak", "\"zec\""), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/publish/publkey/subkey/0/jarak/0/%22zec%22");
    expect_event(pubnub_publish_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 30\r\n\r\n[1,\"Sent\",\"14178940800777403\"]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
}


Ensure(single_context_pubnub, publish_while_busy_fails) {
    pubnub_init(pbp, "pubkey", "subkey");

    /* Leave while leaving */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_leave(pbp, "lamanche"), equals(PNR_STARTED));

    attest(pubnub_publish(pbp, "lamanche", "123"), equals(PNR_IN_PROGRESS));

    expect_event(pubnub_leave_event);
    pubnub_cancel(pbp);
    attest(pubnub_last_result(pbp), equals(PNR_CANCELLED));
}


Ensure(single_context_pubnub, publish_cached_dns_too_long_message) {
    pubnub_init(pbp, "publkey", "subkey");

    char msg[PUBNUB_BUF_MAXLEN + 1];
    memset(msg, 'A', sizeof msg);
    msg[sizeof msg - 1] = '\0';
    attest(pubnub_publish(pbp, "w", msg), equals(PNR_TX_BUFF_TOO_SMALL));

    // URL encoded char
    memset(msg, '"', sizeof msg);
    msg[sizeof msg - 1] = '\0';
    attest(pubnub_publish(pbp, "w", msg), equals(PNR_TX_BUFF_TOO_SMALL));
}


Ensure(single_context_pubnub, subscribe_cached_dns) {
    pubnub_init(pbp, "publkey", "timok");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/morava/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 33\r\n\r\n[[\"Hi\",\"Fi\"],\"14179836755957292\"]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
    attest(pubnub_get(pbp), streqs("\"Hi\""));
    attest(pubnub_get(pbp), streqs("\"Fi\""));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));

    /* Response with channels */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava,lim"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/morava,lim/0/14179836755957292?&pnsdk=PubNub-Contiki-%2F1.1");

    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 59\r\n\r\n[[{\"Wi\"},[\"Xa\"],\"Qi\"],\"14179857817724547\",\"lim,morava,lim\"]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
    attest(pubnub_get(pbp), streqs("{\"Wi\"}"));
    attest(pubnub_get(pbp), streqs("[\"Xa\"]"));
    attest(pubnub_get(pbp), streqs("\"Qi\""));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), streqs("lim"));
    attest(pubnub_get_channel(pbp), streqs("morava"));
    attest(pubnub_get_channel(pbp), streqs("lim"));
    attest(pubnub_get_channel(pbp), equals(NULL));
}


Ensure(single_context_pubnub, subscribe_cached_dns_chunked) {
    pubnub_init(pbp, "publkey", "timok");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/morava/0/0?&pnsdk=PubNub-Contiki-%2F1.1");

    incoming("HTTP/1.1 200\r\nTransfer-Encoding: chunked\r\n\r\n0d\r\n[[1234,\"Da\"],\r\n");
    expect_event(pubnub_subscribe_event);
    incoming("14\r\n\"14179915548467106\"]\r\n0\r\n");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
    attest(pubnub_get(pbp), streqs("1234"));
    attest(pubnub_get(pbp), streqs("\"Da\""));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));

    /* Now "unchunked" */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "ravanica"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/ravanica/0/14179915548467106?&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 33\r\n\r\n[[\"Yo\",1098],\"14179916751973238\"]");

    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
    attest(pubnub_get(pbp), streqs("\"Yo\""));
    attest(pubnub_get(pbp), streqs("1098"));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));
}


Ensure(single_context_pubnub, subscribed_cached_dns_uuid_auth) {
    pubnub_init(pbp, "pubkey", "timok");

    /* Set UID */
    pubnub_set_uuid(pbp, "CECA-CACA-DACA");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "boka"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/boka/0/0?uuid=CECA-CACA-DACA&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 8\r\n\r\n[[],\"0\"]");
    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));

    /* Set auth, too */
    pubnub_set_auth(pbp, "public-key");

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "kotor"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/kotor/0/0?uuid=CECA-CACA-DACA&auth=public-key&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 8\r\n\r\n[[],\"0\"]");
    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));

    /* Reset UUID */
    pubnub_set_uuid(pbp, NULL);

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "sava"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/sava/0/0?auth=public-key&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 8\r\n\r\n[[],\"0\"]");
    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));

    /* Reset auth, too */
    pubnub_set_auth(pbp, NULL);

    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "k"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/k/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 8\r\n\r\n[[],\"0\"]");
    attest(readbuf_left(), equals(0));
    attest(pubnub_last_result(pbp), equals(PNR_OK));
    attest(pubnub_last_http_code(pbp), equals(200));
}


Ensure(single_context_pubnub, subscribe_while_busy_fails) {
    pubnub_init(pbp, "pubkey", "subkey");

    /* Leave while leaving */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_publish(pbp, "nishava", "10987654321"), equals(PNR_STARTED));

    attest(pubnub_subscribe(pbp, "moravica"), equals(PNR_IN_PROGRESS));

    expect_event(pubnub_publish_event);
    pubnub_cancel(pbp);
    attest(pubnub_last_result(pbp), equals(PNR_CANCELLED));
}


Ensure(single_context_pubnub, subscribe_bad_responses) {
    pubnub_init(pbp, "publkey", "timok");

    /* Bad HTTP version */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/morava/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/0.9 200\r\nContent-Length: 33\r\n\r\n[[\"Hi\",\"Fi\"],\"14179836755957292\"]");

    attest(pubnub_last_result(pbp), equals(PNR_IO_ERROR));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));

    /* Response body too long */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/morava/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nContent-Length: 3333\r\n\r\n");

    attest(pubnub_last_result(pbp), equals(PNR_IO_ERROR));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));

    /* Response chunk too long */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/morava/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);
    incoming("HTTP/1.1 200\r\nTransfer-Encoding: chunked\r\n\r\nFFFF\r\n");

    attest(pubnub_last_result(pbp), equals(PNR_IO_ERROR));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));

    /* Response chunk that goes "over the edge" */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/timok/morava/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    expect_event(pubnub_subscribe_event);

    incoming("HTTP/1.1 200\r\nTransfer-Encoding: chunked\r\n\r\nF0\r\n0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\nF0\r\n0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\nF0\r\n");

    attest(pubnub_last_result(pbp), equals(PNR_IO_ERROR));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));
}


Ensure(single_context_pubnub, subscribe_bad_response_content) {
    pubnub_init(pbp, "publkey", "timok");

#define test_(incoming_) do { \
    expect_cached_dns_for_pubnub_origin(); \
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED)); \
    uip_flags = UIP_CONNECTED; \
    expect_outgoing_with_url("/subscribe/timok/morava/0/0?&pnsdk=PubNub-Contiki-%2F1.1"); \
    expect_event(pubnub_subscribe_event); \
    incoming("HTTP/1.0 200\r\nContent-Length: " incoming_); \
    attest(pubnub_last_result(pbp), equals(PNR_FORMAT_ERROR)); \
    attest(pubnub_get(pbp), equals(NULL)); \
    attest(pubnub_get_channel(pbp), equals(NULL)); \
    } while(0)

    /* Does not begin with [ */
    test_("33\r\n\r\nx[\"Hi\",\"Fi\"],\"14179836755957292\"]");

    /* Does not end with ] */
    test_("33\r\n\r\n[[\"Hi\",\"Fi\"],\"14179836755957292\"-");

    /* Last message array element does not end with " */
    test_("33\r\n\r\n[[\"Hi\",\"Fi\"],\"14179836755957292.]");

    /* No string begging in the message (but has ending just before "]") */
    test_("33\r\n\r\n[[SHix,AFif],114179836755957292\"]");

    /* Too big time-token */
    test_("33\r\n\r\n[[1098,7654],\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefX\"]");

    /* No string begging for the "second from the right" in the message */
    test_("31\r\n\r\n[[123,456],114179836755957292\",\"ch\"]");

    /* Too short message (just one character) */
    test_("1\r\n\r\nB");

#undef test_
}


Ensure(single_context_pubnub, tcp_timeout) {
    pubnub_init(pbp, "publkey", "drina");

    /* Connection timeout */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_flags = UIP_TIMEDOUT;

    expect_event(pubnub_subscribe_event);
    incoming("");

    attest(pubnub_last_result(pbp), equals(PNR_TIMEOUT));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));

    /* Timeout while connected */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/drina/morava/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    incoming("");

    uip_flags = UIP_TIMEDOUT;

    expect_event(pubnub_subscribe_event);
    incoming("");

    attest(pubnub_last_result(pbp), equals(PNR_TIMEOUT));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));
}


Ensure(single_context_pubnub, tcp_abort) {
    pubnub_init(pbp, "publkey", "tisa");

    /* Connection timeout */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_abort();

    expect_event(pubnub_subscribe_event);
    incoming("");

    attest(pubnub_last_result(pbp), equals(PNR_IO_ERROR));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));

    /* Timeout while connected */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "tamish"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/tisa/tamish/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    incoming("");

    uip_abort();

    expect_event(pubnub_subscribe_event);
    incoming("");

    attest(pubnub_last_result(pbp), equals(PNR_IO_ERROR));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));
}


Ensure(single_context_pubnub, tcp_closed) {
    pubnub_init(pbp, "kalauz", "drava");

    /* Connecting fails with close */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "morava"), equals(PNR_STARTED));

    uip_close();

    expect_event(pubnub_subscribe_event);
    incoming("");

    attest(pubnub_last_result(pbp), equals(PNR_IO_ERROR));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));

    /* Closed while connected */
    expect_cached_dns_for_pubnub_origin();
    attest(pubnub_subscribe(pbp, "tamish"), equals(PNR_STARTED));

    uip_flags = UIP_CONNECTED;
    expect_outgoing_with_url("/subscribe/drava/tamish/0/0?&pnsdk=PubNub-Contiki-%2F1.1");
    incoming("");

    uip_close();

    expect_event(pubnub_subscribe_event);
    incoming("");

    attest(pubnub_last_result(pbp), equals(PNR_IO_ERROR));
    attest(pubnub_get(pbp), equals(NULL));
    attest(pubnub_get_channel(pbp), equals(NULL));
}
