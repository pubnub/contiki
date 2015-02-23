/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub_ccore.h"

#include <string.h>
#include <stdio.h>


void pbcc_init(struct pbcc_context *p, const char *publish_key, const char *subscribe_key)
{
    p->publish_key = publish_key;
    p->subscribe_key = subscribe_key;
    p->timetoken[0] = '0';
    p->timetoken[1] = '\0';
    p->uuid = p->auth = NULL;
    p->msg_ofs = p->msg_end = 0;
}


char const *pbcc_get_msg(struct pbcc_context *pb)
{
    if (pb->msg_ofs < pb->msg_end) {
        char const *rslt = pb->http_reply + pb->msg_ofs;
        pb->msg_ofs += strlen(rslt);
        if (pb->msg_ofs++ <= pb->msg_end) {
            return rslt;
        }
    }
    
    return NULL;
}


char const *pbcc_get_channel(struct pbcc_context *pb)
{
    if (pb->chan_ofs < pb->chan_end) {
        char const* rslt = pb->http_reply + pb->chan_ofs;
        pb->chan_ofs += strlen(rslt);
        if (pb->chan_ofs++ <= pb->chan_end) {
            return rslt;
        }
    }
    
    return NULL;
}


void pbcc_set_uuid(struct pbcc_context *pb, const char *uuid)
{
    pb->uuid = uuid;
}


void pbcc_set_auth(struct pbcc_context *pb, const char *auth)
{
    pb->auth = auth;
}


/* Find the beginning of a JSON string that comes after comma and ends
 * at @c &buf[len].
 * @return position (index) of the found start or -1 on error. */
static int find_string_start(char const *buf, int len)
{
    int i;
    for (i = len-1; i > 0; --i) {
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
            case ',': if (bracket_level == 0) { *buf = '\0'; } break;
            default: break;
            }
        }
    }

    return !(escaped || in_string || (bracket_level > 0));
}


int pbcc_parse_subscribe_response(struct pbcc_context *p)
{
    char *reply = p->http_reply;
    int replylen = p->http_buf_len;
    if (replylen < 2) {
        return -1;
    }
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
        for (k = replylen - 2; k > i+1; --k) {
            if (reply[k] == ',') {
                reply[k] = 0;
            }
        }
        
        /* ... and look for timetoken again. */
        reply[i-2] = 0;
        p->chan_ofs = i+1;
        i = find_string_start(reply, i-2);
        if (i < 0) {
            p->chan_ofs = 0;
            p->chan_end = 0;
            return -1;
        }
        p->chan_end = replylen - 1;
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


enum pubnub_res pbcc_publish_prep(struct pbcc_context *pb, const char *channel, const char *message)
{
    pb->http_content_len = 0;
    
    pb->http_buf_len = snprintf(
        pb->http_buf, sizeof pb->http_buf,
        "/publish/%s/%s/0/%s/0/", 
        pb->publish_key, pb->subscribe_key, channel
        );
    
    const char *pmessage = message;
    while (pmessage[0]) {
        /* RFC 3986 Unreserved characters plus few
         * safe reserved ones. */
        size_t okspan = strspn(pmessage, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~" ",=:;@[]");
        if (okspan > 0) {
            if (okspan > sizeof(pb->http_buf)-1 - pb->http_buf_len) {
                pb->http_buf_len = 0;
                return PNR_TX_BUFF_TOO_SMALL;
            }
            memcpy(pb->http_buf + pb->http_buf_len, pmessage, okspan);
            pb->http_buf_len += okspan;
            pb->http_buf[pb->http_buf_len] = 0;
            pmessage += okspan;
        }
        if (pmessage[0]) {
            /* %-encode a non-ok character. */
            char enc[4] = {'%'};
            enc[1] = "0123456789ABCDEF"[pmessage[0] / 16];
            enc[2] = "0123456789ABCDEF"[pmessage[0] % 16];
            if (3 > sizeof pb->http_buf - 1 - pb->http_buf_len) {
                pb->http_buf_len = 0;
                return PNR_TX_BUFF_TOO_SMALL;
            }
            memcpy(pb->http_buf + pb->http_buf_len, enc, 4);
            pb->http_buf_len += 3;
            ++pmessage;
        }
    }
    
    return PNR_STARTED;
}


enum pubnub_res pbcc_subscribe_prep(struct pbcc_context *p, const char *channel)
{
    if (p->msg_ofs < p->msg_end) {
        return PNR_RX_BUFF_NOT_EMPTY;
    }

    p->http_content_len = 0;
    p->msg_ofs = 0;
    
    p->http_buf_len = snprintf(p->http_buf, sizeof(p->http_buf),
            "/subscribe/%s/%s/0/%s?" "%s%s" "%s%s%s" "&pnsdk=PubNub-Contiki-%s%%2F%s",
            p->subscribe_key, channel, p->timetoken,
            p->uuid ? "uuid=" : "", p->uuid ? p->uuid : "",
            p->uuid && p->auth ? "&" : "",
            p->auth ? "auth=" : "", p->auth ? p->auth : "",
            "", "1.1"
            );

    return PNR_STARTED;
}


enum pubnub_res pbcc_leave_prep(struct pbcc_context *p, const char *channel)
{
    p->http_content_len = 0;
    
    /* Make sure next subscribe() will be a join. */
    p->timetoken[0] = '0';
    p->timetoken[1] = '\0';
    
    p->http_buf_len = snprintf(p->http_buf, sizeof(p->http_buf),
            "/v2/presence/sub-key/%s/channel/%s/leave?" "%s%s" "%s%s%s",
            p->subscribe_key, 
            channel,
            p->uuid ? "uuid=" : "", p->uuid ? p->uuid : "",
            p->uuid && p->auth ? "&" : "",
            p->auth ? "auth=" : "", p->auth ? p->auth : "");

    return PNR_STARTED;
}
