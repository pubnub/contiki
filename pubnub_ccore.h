/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if !defined INC_PUBNUB_CCORE
#define      INC_PUBNUB_CCORE

#include "pubnub.h"

/** @file pubnub_ccore.h 

    The "C core" module is the internal module of the Pubnub C
    clients, shared by all. It has all the "generic" code (formatting,
    parsing, HTTP...)  while individual C clients deal with
    interacting with the environment (OS, frameworks, libraries...).

    The interface of this module is subject to change, so user of a
    Pubnub C client should *not* use this module directly. Use the
    official and documented API for your environment.
*/


/** The Pubnub "(C) core" context, contains context data 
    that is shared among all Pubnub C clients.
 */
struct pbcc_context {
    /** The publish key (to use when publishing) */
    char const *publish_key;
    /** The subscribe key (to use when subscribing) */
    char const *subscribe_key;
    /** The UUID to be sent on to server. If NULL, don't send any */
    char const *uuid;
    /** The `auth` parameter to be sent on to server. If NULL, don't send any */
    char const *auth;
    /** The last used time token. */
    char timetoken[64];

    /** The result of the last Pubnub transaction */
    enum pubnub_res last_result;

    /** The buffer for HTTP data */
    char http_buf[PUBNUB_BUF_MAXLEN];
    /** Last received HTTP (result) code */
    int http_code;
    /** The length of the data in the HTTP buffer */
    unsigned http_buf_len;
    /** The length of total data to be received in a HTTP reply */
    unsigned http_content_len;
    /** Indicates whether we are receiving chunked or regular HTTP response */
    bool http_chunked;
    /** The contents of a HTTP reply/reponse */
    char http_reply[PUBNUB_REPLY_MAXLEN+1];

    /* These in-string offsets are used for yielding messages received
     * by subscribe - the beginning of last yielded message and total
     * length of message buffer, and the same for channels.
     */
    unsigned short msg_ofs, msg_end, chan_ofs, chan_end;

};


/** Initializes the Pubnub C core context */
void pbcc_init(struct pbcc_context *pbcc, const char *publish_key, const char *subscribe_key);

/** Returns the next message from the Pubnub C Core context. NULL if
    there are no (more) messages
*/
char const *pbcc_get_msg(struct pbcc_context *pb);

/** Returns the next channel from the Pubnub C Core context. NULL if
    there are no (more) messages.
*/
char const *pbcc_get_channel(struct pbcc_context *pb);

/** Sets the UUID for the context */
void pbcc_set_uuid(struct pbcc_context *pb, const char *uuid);

/** Sets the `auth` for the context */
void pbcc_set_auth(struct pbcc_context *pb, const char *auth);

/** Parses the string received as a response for a subscribe operation
    (transaction). This checks if the response is valid, and, if it
    is, prepares for giving the messages (and possibly channels) that
    are received in the response to the user (via pbcc_get_msg() and
    pbcc_get_channel()).

    @param p The Pubnub C core context to parse the response "in"
    @return 0: OK, -1: error (invalid response)
*/
int pbcc_parse_subscribe_response(struct pbcc_context *p);

/** Prepares the Publish operation (transaction), mostly by
    formatting the URI of the HTTP request.
 */
enum pubnub_res pbcc_publish_prep(struct pbcc_context *pb, const char *channel, const char *message);

/** Prepares the Subscribe operation (transaction), mostly by
    formatting the URI of the HTTP request.
 */
enum pubnub_res pbcc_subscribe_prep(struct pbcc_context *p, const char *channel);

/** Prepares the Leave operation (transaction), mostly by
    formatting the URI of the HTTP request.
 */
enum pubnub_res pbcc_leave_prep(struct pbcc_context *p, const char *channel);


#endif /* !defined INC_PUBNUB_CCORE */
