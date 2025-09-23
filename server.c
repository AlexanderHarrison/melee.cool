#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vendor/hescape.h"

#define MG_TLS MG_TLS_OPENSSL
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
#define countof(A) (sizeof(A) / sizeof(*(A)))

#include "clips.c"

void mg_print_str(const char *preface, struct mg_str str) {
    printf("%s%.*s\n", preface, (int) str.len, str.buf);
}

#define REPLYSIZE (1024*1024)
static Str reply;
static Str reply_headers;
static const char default_reply_headers[] = "Content-Type: text/html; charset=utf-8\r\n";

#define reply_headers_push_const(s) do {\
    const char _str[] = s;\
    USize _len = sizeof(_str)-1;\
    if (reply_headers.len + _len <= REPLYSIZE)\
        memcpy(reply_headers.buf + reply_headers.len, _str, _len);\
    reply_headers.len += _len;\
} while (0)

void reply_headers_push_slice(const char *buf, USize len) {
    if (reply_headers.len + len <= REPLYSIZE)
        memcpy(reply_headers.buf + reply_headers.len, buf, len);
    reply_headers.len += len;
}

#define reply_push_const(s) do {\
    const char _str[] = s;\
    USize _len = sizeof(_str)-1;\
    if (reply.len + _len <= REPLYSIZE)\
        memcpy(reply.buf + reply.len, _str, _len);\
    reply.len += _len;\
} while (0)

void reply_push(const char *str) {
    USize str_len = strlen(str);
    if (reply.len + str_len <= REPLYSIZE)
        memcpy(reply.buf + reply.len, str, str_len);
    reply.len += str_len;
}

void reply_push_slice(const char *buf, USize len) {
    if (reply.len + len <= REPLYSIZE)
        memcpy(reply.buf + reply.len, buf, len);
    reply.len += len;
}

void reply_clips(EntrySearch *search, bool reverse_search) {
    Entry *entries[10];
    USize entry_count = 0;
    
    if (reverse_search) {
        for (; entry_count != 10; ++entry_count) {
            Entry *entry = search_prev(search);
            if (entry == NULL) { break; }
            entries[entry_count] = entry;
        }
        // reverse entry order
        for (USize i = 0; i < entry_count/2; ++i) {
            Entry *a = entries[i];
            Entry *b = entries[entry_count - i - 1];
            entries[i] = b;
            entries[entry_count - i - 1] = a;
        }
    } else {
        for (; entry_count != 10; ++entry_count) {
            Entry *entry = search_next(search);
            if (entry == NULL) { break; }
            entries[entry_count] = entry;
        }
    }

    reply_push_const("<div id=clips hx-swap-oob=true>");
    
    if (entry_count == 0)
        reply_push_const("No results");
    
    for (U32 entry_i = 0; entry_i != entry_count; ++entry_i) {
        Entry *entry = entries[entry_i];

        reply_push_const("<div class=clip>");
            reply_push_const("<div class=metadata>");
                const char *metadata = entry_metadata(entry);
                printf("- Found entry %s\n", metadata);
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
    if (reply.len <= REPLYSIZE && reply_headers.len < REPLYSIZE) {
        // Headers are cstrings for some reason????
        reply_headers.buf[reply_headers.len] = 0;

        mg_http_reply(c, 200, reply_headers.buf, "%.*s", (int)reply.len, reply.buf);
    } else {
        mg_http_reply(c, 400, default_reply_headers, "Response is too large for internal buffer.");
    }

    reply.len = 0;
    reply_headers.len = sizeof(default_reply_headers)-1;
}

static const char hex_decode_lut[256] = {
    ['0'] = 0, ['1'] = 1, ['2'] = 2, ['3'] = 3, ['4'] = 4,
    ['5'] = 5, ['6'] = 6, ['7'] = 7, ['8'] = 8, ['9'] = 9,
    ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15,
    ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15,
};

void format_uint_hex(char buf[8], U32 n) {
    char *b = buf;

    USize i = 28;
    while (1) {
        U32 digit = (n >> i) & 0xF;
        U32 ch_start = digit < 10 ? '0' : ('A' - 10);
        char c = (char)(digit + ch_start);

        *b = c;
        b++;
        
        if (i == 0) break;
        i -= 4;
    }
}

U32 parse_uint_hex(Str s) {
    U32 n = 0;
    for (U32 l = 0; l < s.len; ++l) {
        char c = s.buf[l];
        n = (n << 4) | (U32)hex_decode_lut[c];
    }
    return n;
}

void decode_uri(Str *uri_ptr) {
    char *buf = uri_ptr->buf;
    USize len = uri_ptr->len;
    
    USize i_new = 0;
    USize i_old = 0;
    
    while (i_old != len) {
        char c_old = buf[i_old];
        char c_new = c_old;
        if (c_old != '%') {
            i_old++;
        }
        // check for malformed uri
        else if (i_old + 2 >= len) {
            i_old++;
        }
        else {
            char d1 = buf[i_old+1];
            char d2 = buf[i_old+2];
            c_new = (hex_decode_lut[d1] << 4) | hex_decode_lut[d2];
            i_old += 3;
        }
        
        buf[i_new] = (char)c_new;
        i_new++;
    }
    
    *uri_ptr = (Str) { buf, i_new };
}

USize split_idx(Str idx, U32 idxbuf[TAGMAX]) {
    USize idx_i = 0;
    USize idx_count = 0;
    while (idx_count != TAGMAX) {
        while (idx_i != idx.len && idx.buf[idx_i] == '-')
            idx_i++;
        USize idx_start = idx_i;
        
        // find idx end
        while (idx_i != idx.len && idx.buf[idx_i] != '-')
            idx_i++;
        USize idx_end = idx_i;
        
        if (idx_end == idx_start)
            break;
        
        Str idx_str = { idx.buf + idx_start, idx_end - idx_start };
        idxbuf[idx_count++] = parse_uint_hex(idx_str);
    }

    return idx_count;
}

USize split_tags(Str tags, Str tagbuf[TAGMAX]) {
    USize tag_i = 0;
    USize tag_count = 0;
    while (tag_count != TAGMAX) {
        // skip whitespace
        while (tag_i != tags.len && tags.buf[tag_i] == ' ')
            tag_i++;
        USize tag_start = tag_i;
        
        // find tag end
        while (tag_i != tags.len && tags.buf[tag_i] != ' ')
            tag_i++;
        USize tag_end = tag_i;
        
        if (tag_end == tag_start)
            break;
        
        Str tag = { tags.buf + tag_start, tag_end - tag_start };
        tagbuf[tag_count++] = tag;
    }

    return tag_count;
}

void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        
        mg_print_str("URI: ", hm->uri);
        
        if (mg_match(hm->method, mg_str("GET"), NULL)) {
            if (mg_match(hm->uri, mg_str("/htmx.js"), NULL)) {
                static struct mg_http_serve_opts opts = { 0 };
                mg_http_serve_file(c, hm, "vendor/htmx.js", &opts);
            }
            else {
                static struct mg_http_serve_opts opts = { .root_dir = "web_root" };
                mg_http_serve_dir(c, hm, &opts);
            }
        }
        else if (mg_match(hm->method, mg_str("POST"), NULL)) {
            if (mg_match(hm->uri, mg_str("/find-clips"), NULL)) {
                Str tags_str = mg_http_var(hm->body, mg_str("tags"));
                Str search_idx_str = mg_http_var(hm->body, mg_str("idx"));
                Str rev_str = mg_http_var(hm->body, mg_str("rev"));
                bool reverse_search = rev_str.len != 0;

                mg_print_str("- search idx str: ", search_idx_str);
                U32 search_idx = parse_uint_hex(search_idx_str);
                printf("- search idx: %x\n", search_idx);

                EntrySearch search = { 0 };
                
                U32 idx[TAGMAX];
                U32 idx_count = (U32)split_idx(search_idx_str, idx);
                for (USize i = 0; i < idx_count; ++i)
                    search.tag_search_idx[i] = idx[i];

                Str tags_str_decoded = mg_strdup(tags_str);
                Str tags[TAGMAX];
                decode_uri(&tags_str_decoded);
                USize tag_count = split_tags(tags_str_decoded, tags);
                
                for (USize i = 0; i < tag_count; ++i) {
                    Str tag = tags[i];
                    mg_print_str("- Find tag: ", tag);
                    search_tag(&search, tag);
                }

                reply_clips(&search, reverse_search);
                mg_free(tags_str_decoded.buf);
                
                reply_headers_push_const("Hx-Push-Url: /clips/?");
                
                {
                    U32 *idx_given = idx;
                    U32 idx_given_count = idx_count;
                    
                    if (reverse_search)
                        reply_headers_push_const("idx-b=");
                    else
                        reply_headers_push_const("idx-a=");

                    for (USize i = 0; i < idx_given_count; ++i) {
                        if (i) reply_headers_push_const("-");

                        char idx_str[8];
                        format_uint_hex(idx_str, idx_given[i]);
                        char *idx_buf = idx_str;
                        USize idx_len = countof(idx_str);
                        while (idx_len && *idx_buf == '0') { idx_buf++; idx_len--; }
                        reply_headers_push_slice(idx_buf, idx_len);
                    }
                }

                {
                    U32 *idx_new = search.tag_search_idx;
                    U32 idx_new_count = search.tag_count;
                    if (idx_new_count == 0) idx_new_count = 1;

                    if (reverse_search)
                        reply_headers_push_const("&idx-a=");
                    else
                        reply_headers_push_const("&idx-b=");

                    for (USize i = 0; i < idx_new_count; ++i) {
                        if (i) reply_headers_push_const("-");

                        char idx_str[8];
                        format_uint_hex(idx_str, idx_new[i]);
                        char *idx_buf = idx_str;
                        USize idx_len = countof(idx_str);
                        while (idx_len && *idx_buf == '0') { idx_buf++; idx_len--; }
                        reply_headers_push_slice(idx_buf, idx_len);
                    }
                }

                if (tag_count) {
                    reply_headers_push_const("&tags=");
                    reply_headers_push_slice(tags_str.buf, tags_str.len);
                }
                
                reply_headers_push_const("\r\n");
                
                reply_send(c);
            }
            else if (mg_match(hm->uri, mg_str("/post-clip"), NULL)) {
                Str metadata = mg_http_var(hm->body, mg_str("metadata"));
                EntryIdx entry = create_entry(metadata);
                mg_print_str("- Added entry: \n", metadata);
                
                Str tags_str = mg_http_var(hm->body, mg_str("tags"));
                decode_uri(&tags_str);
                Str tags[TAGMAX];
                USize tag_count = split_tags(tags_str, tags);
                for (USize i = 0; i < tag_count; ++i) {
                    Str tag = tags[i];
                    mg_print_str("- Tagged: ", tag);
                    tag_entry(entry, tag);
                }
                
                EntrySearch search = { 0 };
                reply_clips(&search, false);
                reply_send(c);
            }
        }
    }
    else if (ev == MG_EV_ACCEPT && c->is_tls) {
        struct mg_tls_opts opts = {
            .cert = mg_file_read(&mg_fs_posix, "certs/domain.cert.pem"),
            .key = mg_file_read(&mg_fs_posix, "certs/private.key.pem"),
        };
        mg_tls_init(c, &opts);
    }
}

int main(void) {
    init_clip_tables();
    create_entries(1 << 16, 8);
    
    reply.buf = calloc(REPLYSIZE, 1);
    
    reply_headers.buf = calloc(REPLYSIZE, 1);
    memcpy(reply_headers.buf, default_reply_headers, sizeof(default_reply_headers)-1);
    reply_headers.len = sizeof(default_reply_headers)-1;

    mg_log_set(MG_LL_DEBUG);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    // mg_http_listen(&mgr, "http://0.0.0.0:8000", ev_handler, NULL);
    mg_http_listen(&mgr, "http://0.0.0.0:80", ev_handler, NULL);
    mg_http_listen(&mgr, "https://0.0.0.0:443", ev_handler, NULL);

    while (true)
        mg_mgr_poll(&mgr, -1);
    mg_mgr_free(&mgr);
    
    return 0;
}

