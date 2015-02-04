/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#ifndef PUBNUB_H
#define	PUBNUB_H

#include <stdbool.h>

#include "contiki.h"

/** @mainpage The ConTiki OS Pubnub client library

    This is the Pubnub client library for the ConTiki OS. It is
    carefully designed for small footprint and to be a good fit for
    ConTiki OS way of multitasking with Protothreads and Contiki OS
    processes. You can have multiple pubnub contexts established; in
    each context, at most one Pubnub API call/transaction may be
    ongoing (typically a "publish" or a "subscribe").

    It has less features than most Pubnub libraries for other OSes, as
    it is designed to be used in more constrained environments.

    The most important differences from a full-fledged Pubnub API
    implementation are:

    - The only available Pubnub APIs are: publish, subscribe, leave.

    - Library itself doesn't handle timeouts other than TCP/IP
    timeout, which mostly comes down to loss of connection to the
    server. If you want to impose a timeout on transaction duration,
    use one of the several ConTiki OS timer interfaces yourself.

    - You can't change the origin (URL) or several other parameters of
    connection to Pubnub.
    
 */

/** @file pubnub.h */

/* -- Next few definitions can be tweaked by the user, but with care -- */

/** Maximum number of PubNub contexts.
 * A context is used to publish messages or subscribe (get) them.
 *
 * Doesn't make much sense to have less than 1. :)
 * OTOH, don't put too many, as each context takes (for our purposes)
 * a significant amount of memory - app. 128 + @ref PUBNUB_BUF_MAXLEN +
 * @ref PUBNUB_REPLY_MAXLEN bytes.
 *
 * A typical configuration may consist of a single pubnub context for
 * channel subscription and another pubnub context that will periodically
 * publish messages about device status (with timeout lower than message
 * generation frequency).
 *
 * Another typical setup may have a single subscription context and
 * maintain a pool of contexts for each publish call triggered by an
 * external event (e.g. a button push).
 *
 * Of course, there is nothing wrong with having just one context, but
 * you can't publish and subscribe at the same time on the same context.
 * This isn't as bad as it sounds, but may be a source of headaches
 * (lost messages, etc).
 */
#define PUBNUB_CTX_MAX 2

/** Maximum length of the HTTP buffer. This is a major component of
 * the memory size of the whole pubnub context, but it is also an
 * upper bound on URL-encoded form of published message, so if you
 * need to construct big messages, you may need to raise this.  */
#define PUBNUB_BUF_MAXLEN 256

/** Maximum length of the HTTP reply. The other major component of the
 * memory size of the PubNub context, beside #PUBNUB_BUF_MAXLEN.
 * Replies of API calls longer than this will be discarded and
 * instead, #PNR_FORMAT_ERROR will be reported. Specifically, this may
 * cause lost messages returned by subscribe if too many too large
 * messages got queued on the Pubnub server. */
#define PUBNUB_REPLY_MAXLEN 512

/** If defined, the PubNub implementation will not try to catch-up on
 * messages it could miss while subscribe failed with an IO error or
 * such.  Use this if missing some messages is not a problem.  
 *
 * @note messages may sometimes still be lost due to potential @ref
 * PUBNUB_REPLY_MAXLEN overrun issue */
#define PUBNUB_MISSMSG_OK 1

/** This is the URL of the Pubnub server. Change only for testing
    purposes.
*/
#define PUBNUB_ORIGIN  "pubsub.pubnub.com"

#if !defined PUBNUB_USE_MDNS
/** If `1`, the MDNS module will be used to handle the DNS
        resolving. If `0` the "resolv" module will be used.
        This is a temporary solution, it is expected that ConTiki
        will unify those two modules.
*/
#define PUBNUB_USE_MDNS 1
#endif

/* -- You should not change anything below this line -- */

struct pubnub;

/** A pubnub context. An opaque data structure that holds all the
    data needed for a context.
 */
typedef struct pubnub pubnub_t;

/** Result codes for Pubnub functions and transactions. 
*/
enum pubnub_res {
    /** Success. Transaction finished successfully. */
    PNR_OK,
    /** Time out before the request has completed. */
    PNR_TIMEOUT,
    /** Connection to Pubnub aborted (reset) */
    PNR_ABORTED,
    /** Communication error (network or HTTP response format). */
    PNR_IO_ERROR,
    /** HTTP error. */
    PNR_HTTP_ERROR,
    /** Unexpected input in received JSON. */
    PNR_FORMAT_ERROR,
    /** Request cancelled by user. */
    PNR_CANCELLED,
    /** Transaction started. Await the outcome via process message. */
    PNR_STARTED,
    /** Transaction (already) ongoing. Can't start a new transaction. */
    PNR_IN_PROGRESS,
    /** Receive buffer (from previous transaction) not read, new
        subscription not allowed.
    */
    PNR_RX_BUFF_NOT_EMPTY,
    /** The buffer is to small. Increase #PUBNUB_BUF_MAXLEN.
    */
    PNR_TX_BUFF_TOO_SMALL
};


/** Returns a context for the given index. Contexts are statically
    allocated by the Pubnub library and this is the only way to
    get a pointer to one of them.

    @param index The index of the context
    @pre (index >= 0) && (index < #PUBNUB_CTX_MAX)
    @return Context pointer on success
 */
pubnub_t *pubnub_get_ctx(unsigned index);


/** Initialize a given pubnub context @p p to the @p publish_key and @p
    subscribe_key. You can customize other parameters of the context by
    the configuration function calls below.  

    @note The @p publish_key and @p subscribe key are expected to be
    valid (ASCIIZ string) pointers throughout the use of context @p p,
    that is, until either you call pubnub_done(), or the otherwise
    stop using it (like when the whole software/ firmware stops
    working). So, the contents of these keys are not copied to the
    Pubnub context @p p.

    @pre Call this after TCP initialization.
    @param p The Context to initialize (use pubnub_get_ctx() to
    obtain it)
    @param publish_key The string of the key to use when publishing
    messages
    @param subscribe_key The string of the key to use when subscribing
    to messages
*/
void pubnub_init(pubnub_t *p, const char *publish_key, const char *subscribe_key);


/** Deinitialize a given pubnub context, freeing all its associated
    resources.  Needs to be called only if you manage multiple pubnub
    contexts dynamically. 
*/
void pubnub_done(pubnub_t *p);

/** Set the UUID identification of PubNub client context @p p to @p
    uuid. Pass NULL to unset.

    @note The @p uuid is expected to be valid (ASCIIZ string) pointers
    throughout the use of context @p p, that is, until either you call
    pubnub_done() on @p p, or the otherwise stop using it (like when
    the whole software/ firmware stops working). So, the contents of
    the @p uuid string is not copied to the Pubnub context @p p.  */
void pubnub_set_uuid(pubnub_t *p, const char *uuid);

/** Set the authentication information of PubNub client context @p
    p. Pass NULL to unset.

    @note The @p uuid is expected to be valid (ASCIIZ string) pointers
    throughout the use of context @p p, that is, until either you call
    pubnub_done() on @p p, or the otherwise stop using it (like when
    the whole software/ firmware stops working). So, the contents of
    the uuid string is not copied to the Pubnub context @p p.  */
void pubnub_set_auth(pubnub_t *p, const char *auth);

/** Cancel an ongoing API transaction. The outcome of the transaction
    in progress will be #PNR_CANCELLED. */
void pubnub_cancel(pubnub_t *p);

/** Publish the @p message (in JSON format) on @p p channel, using the
    @p p context. This actually means "initiate a publish
    transaction". The outcome is sent to the process that starts the
    transaction via process event #pubnub_publish_event. You don't
    have to do any special processing of said event - use it at your
    own convinience (you may retry on failure, for example).

    You can't publish if a transaction is in progress in @p p context.

    @param p The pubnub context. Can't be NULL
    @param channel The string with the channel (or comma-delimited list
    of channels) to publish to.
    @param message The message to publish, expected to be in JSON format

    @return #PNR_STARTED on success, an error otherwise
 */
enum pubnub_res pubnub_publish(pubnub_t *p, const char *channel, const char *message);

/** The ID of the Pubnub Publish event. Event carries the context pointer
    on which the publish transaction finished. Use pubnub_last_result()
    to read the outcome of the transaction.
 */
extern process_event_t pubnub_publish_event;

/** Returns a pointer to an arrived message. Message(s) arrive on
    finish of a subscribe transaction. Subsequent call to this
    function will return the next message (if any). All messages
    are from the channel(s) the last subscription was for.

    @note Context doesn't keep track of the channel(s) you subscribed
    to. This is a memory saving design decision, as most users won't
    change the channel(s) they subscribe too.

    @param p The Pubnub context. Can't be NULL.

    @return Pointer to the message, NULL on error
    @see pubnub_subscribe
 */
char const* pubnub_get(pubnub_t *p);

/** Returns a pointer to an fetched transaction's next channel.  Each
    transaction may hold a list of channels, and this functions
    provides a way to read them.  Subsequent call to this function
    will return the next channel (if any).

    @note You don't have to read all (or any) of the channels before
    you start a new subscribe transaction.

    @param pb The Pubnub context. Can't be NULL.

    @return Pointer to the channel, NULL on error
    @see pubnub_subscribe
    @see pubnub_get
 */
char const *pubnub_get_channel(pubnub_t *pb);

/** Subscribe to @p channel. This actually means "initiate a subscribe
    transaction". The outcome is sent to the process that starts the
    transaction via process event #pubnub_publish_event, which is a
    good place to start reading the fetched message(s), via
    pubnub_get().

    Messages published on @p p channel since the last subscribe
    transaction will be fetched.

    The @p channel string may contain multiple comma-separated channel
    names, so only one call is needed to fetch messages from multiple
    channels.

    You can't subscribe if a transaction is in progress on the context.

    Also, you can't subscribe if there are unread messages in the
    context (you read messages with pubnub_get()).

    @note Some of the subscribed messages may be lost when calling
    publish() after a subscribe() on the same context or subscribe()
    on different channels in turn on the same context.  But typically,
    you will want two separate contexts for publish and subscribe
    anyway. If you are changing the set of channels you subscribe to,
    you should first call pubnub_leave() on the old set.

    @param p The pubnub context. Can't be NULL
    @param channel The string with the channel name (or comma-delimited list
    of channel names) to subscribe to.

    @return #PNR_STARTED on success, an error otherwise
    
    @see pubnub_get
 */
enum pubnub_res pubnub_subscribe(pubnub_t *p, const char *channel);

/** The ID of the Pubnub Subscribe event. Event carries the context
    pointer on which the subscribe transaction finished. Use
    pubnub_last_result() to read the outcome of the transaction.
 */
extern process_event_t pubnub_subscribe_event;

/** Leave the @p channel. This actually means "initiate a leave
    transaction".  You should leave a channel when you want to
    subscribe to another in the same context to avoid loosing
    messages.

    The outcome is sent to you via a process event,
    which you are free to ignore, but is a good place to start
    subscribe to another channel, via pubnub_get().
    
    You can't leave if a transaction is in progress on the context.

    @param p The Pubnub context. Can't be NULL.  
    @param channel The string with the channel name (or
    comma-delimited list of channel names) to subscribe to.

    @return #PNR_STARTED on success, an error otherwise
*/
enum pubnub_res pubnub_leave(pubnub_t *p, const char *channel);

/** The ID of the Pubnub Leave event. Event carries the context
    pointer on which the leave transaction finished. Use 
    pubnub_last_result() to read the outcome of the transaction.
 */
extern process_event_t pubnub_leave_event;

/** Returns the result of the last transaction in the @p p context. */
enum pubnub_res pubnub_last_result(pubnub_t const *p);

/** Returns the HTTP reply code of the last transaction in the @p p
 * context. */
int pubnub_last_http_code(pubnub_t const *p);

PROCESS_NAME(pubnub_process);


#endif        /* PUBNUB_H */
