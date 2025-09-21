#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vendor/hescape.h"
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

// void mg_print_str(const char *preface, struct mg_str str) {
//     printf("%s%.*s\n", preface, (int) str.len, str.buf);
// }

void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        // printf("%.*s %.*s\n", (int)hm->method.len, hm->method.buf, (int)hm->uri.len, hm->uri.buf);
        
        static struct mg_http_serve_opts opts = { .root_dir = "web_root" };
        mg_http_serve_dir(c, hm, &opts);
        
        #if 0
            if (mg_match(hm->method, mg_str("GET"), NULL)) {
                if (mg_match(hm->uri, mg_str("/clips"), NULL)) {
                    mg_http_serve_file(c, hm, "clips.html", &opts);
                    printf("- Served clips.html\n");
                }
                else if (mg_match(hm->uri, mg_str("/htmx.js"), NULL)) {
                    mg_http_serve_file(c, hm, "vendor/htmx.js", &opts);
                    printf("- Served vendor/htmx.js\n");
                }
                else if (mg_match(hm->uri, mg_str("/recent-clips"), NULL)) {
                    reply_recent_clips();
                    reply_send(c);
                }
                else {
                    mg_http_reply(c, 404, NULL, "");
                    printf("- 404\n");
                }
            }
            else if (mg_match(hm->method, mg_str("POST"), NULL)) {
                if (mg_match(hm->uri, mg_str("/post-clip"), NULL)) {
                    Str metadata = mg_http_var(hm->body, mg_str("metadata"));
                    EntryIdx entry = create_entry(metadata);
                    mg_print_str("- Added entry: \n", metadata);
                    
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
                    printf("- Served recent clips\n");
                }
            }
        #endif
 
        return;
    }
}

int main(void) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:10000", ev_handler, NULL);
    while (true)
        mg_mgr_poll(&mgr, -1);
    mg_mgr_free(&mgr);
    
    return 0;
}
