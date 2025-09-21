#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vendor/hescape.h"

#define MG_TLS MG_TLS_BUILTIN
#include "vendor/mongoose.h"

typedef double F64;
typedef float F32;
typedef uint64_t U64;
typedef uint32_t U32;
typedef uint16_t U16;
typedef uint8_t U8;
typedef int64_t I64;
typedef int32_t I32;
typedef int16_t I16;
typedef int8_t I8;
typedef size_t USize;
typedef ssize_t ISize;
typedef struct mg_str Str;

#include "clips.c"

void mg_print_str(const char *preface, struct mg_str str) {
    printf("%s%.*s\n", preface, (int) str.len, str.buf);
}

#define REPLYSIZE (1024*1024)
static Str reply;
static const char *reply_headers = "Content-Type: text/html; charset=utf-8\r\n";

#define reply_push_const(s) do {\
    const char _str[] = s;\
    USize _len = sizeof(_str)-1;\
    if (reply.len + _len <= REPLYSIZE)\
        memcpy(reply.buf + reply.len, _str, _len);\
    reply.len += _len;\
} while (0)

void reply_push(const char *str) {
    USize str_len = strlen(str);
    memcpy(reply.buf + reply.len, str, str_len);
    reply.len += str_len;
}

void reply_push_slice(const char *buf, USize len) {
    memcpy(reply.buf + reply.len, buf, len);
    reply.len += len;
}

void reply_recent_clips(void) {
    reply_push_const("<div id=clips hx-swap-oob=true>");
    EntrySearch search = { 0 };
    for (U32 clip_i = 0; clip_i != 10; ++clip_i) {
        Entry *entry = search_next(&search);
        if (entry == NULL)
            break;
        
        reply_push_const("<div class=clip>");
            reply_push_const("<div class=metadata>");
                const char *metadata = entry_metadata(entry);
                printf("- found %s\n", metadata);
                reply_push(metadata);
            reply_push_const("</div>");
            reply_push_const("<div class=tags>");
                const char *tags = entry_tags(entry);
                while (*tags) {
                    USize i = 0;
                    for (; tags[i] != '\n'; ++i);
                    reply_push_const("<div class=tag>");
                        reply_push_slice(tags, i);
                    reply_push_const("</div>");
                    tags += i + 1;
                }
            reply_push_const("</div>");
        reply_push_const("</div>");
    }
    reply_push_const("</div>");
}

void reply_send(struct mg_connection *c) {
    if (reply.len <= REPLYSIZE) {
        mg_http_reply(c, 200, reply_headers, "%.*s", (int)reply.len, reply.buf);
    } else {
        mg_http_reply(c, 400, reply_headers, "Response is too large for internal buffer.");
    }
    reply.len = 0;
}

void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        // printf("%.*s %.*s\n", (int)hm->method.len, hm->method.buf, (int)hm->uri.len, hm->uri.buf);
        
        if (mg_match(hm->method, mg_str("GET"), NULL)) {
            if (mg_match(hm->uri, mg_str("/htmx.js"), NULL)) {
                static struct mg_http_serve_opts opts = { 0 };
                mg_http_serve_file(c, hm, "vendor/htmx.js", &opts);
            }
            else if (mg_match(hm->uri, mg_str("/recent-clips"), NULL)) {
                reply_recent_clips();
                reply_send(c);
            }
            else {
                static struct mg_http_serve_opts opts = { .root_dir = "web_root" };
                mg_http_serve_dir(c, hm, &opts);
            }
        }
        else if (mg_match(hm->method, mg_str("POST"), NULL)) {
            if (mg_match(hm->uri, mg_str("/post-clip"), NULL)) {
                Str metadata = mg_http_var(hm->body, mg_str("metadata"));
                EntryIdx entry = create_entry(metadata);
                // mg_print_str("- Added entry: \n", metadata);
                
                Str tag1 = mg_http_var(hm->body, mg_str("tag1"));
                if (tag1.len)
                    tag_entry(entry, tag1);
                Str tag2 = mg_http_var(hm->body, mg_str("tag2"));
                if (tag2.len)
                    tag_entry(entry, tag2);
                Str tag3 = mg_http_var(hm->body, mg_str("tag3"));
                if (tag3.len)
                    tag_entry(entry, tag3);
                
                reply_recent_clips();
                reply_send(c);
            }
        }
    } else if (ev == MG_EV_ACCEPT) {
        struct mg_tls_opts opts = {
            .cert = mg_unpacked("certs/domain.cert.pem"),
            .key = mg_unpacked("certs/private.key.pem")
        };
        mg_tls_init(c, &opts);
    }
}

int main(void) {
    init_clip_tables();
    reply.buf = calloc(REPLYSIZE, 1);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    mg_http_listen(&mgr, "http://0.0.0.0:10000", ev_handler, NULL);
    while (true)
        mg_mgr_poll(&mgr, -1);
    mg_mgr_free(&mgr);
    
    return 0;
}
