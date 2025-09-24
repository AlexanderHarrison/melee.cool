#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

char html_char_escape_lut[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 
    32, 33, 38, 113, 117, 111, 116, 59, 35, 36, 37, 38, 97, 109, 112, 59, 
    38, 35, 120, 50, 55, 59, 40, 41, 42, 43, 44, 45, 46, 38, 35, 120, 
    50, 70, 59, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 38, 
    108, 116, 59, 38, 35, 120, 51, 68, 59, 38, 103, 116, 59, 63, 64, 65, 
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 38, 103, 
    114, 97, 118, 101, 59, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 
    108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 
    124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 
    140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 
    156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 
    172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 
    188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 
    204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 
    220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 
    236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 
    252, 253, 254, 255,
    
    // pad a little bit for memcpy overread
    0, 0, 0, 0, 0, 0, 0, 0,
};

// high 6 bits: string size
// low 10 bits: offset into html_char_escape_lut to find string
U16 html_char_size_lut[256] = {
    1024, 1025, 1026, 1027, 1028, 1029, 1030, 1031, 1032, 1033, 1034, 1035, 1036, 1037, 1038, 1039, 
    1040, 1041, 1042, 1043, 1044, 1045, 1046, 1047, 1048, 1049, 1050, 1051, 1052, 1053, 1054, 1055, 
    1056, 1057, 6178, 1064, 1065, 1066, 5163, 6192, 1078, 1079, 1080, 1081, 1082, 1083, 1084, 6205, 
    1091, 1092, 1093, 1094, 1095, 1096, 1097, 1098, 1099, 1100, 1101, 1102, 4175, 6227, 4185, 1117, 
    1118, 1119, 1120, 1121, 1122, 1123, 1124, 1125, 1126, 1127, 1128, 1129, 1130, 1131, 1132, 1133, 
    1134, 1135, 1136, 1137, 1138, 1139, 1140, 1141, 1142, 1143, 1144, 1145, 1146, 1147, 1148, 1149, 
    7294, 1157, 1158, 1159, 1160, 1161, 1162, 1163, 1164, 1165, 1166, 1167, 1168, 1169, 1170, 1171, 
    1172, 1173, 1174, 1175, 1176, 1177, 1178, 1179, 1180, 1181, 1182, 1183, 1184, 1185, 1186, 1187, 
    1188, 1189, 1190, 1191, 1192, 1193, 1194, 1195, 1196, 1197, 1198, 1199, 1200, 1201, 1202, 1203, 
    1204, 1205, 1206, 1207, 1208, 1209, 1210, 1211, 1212, 1213, 1214, 1215, 1216, 1217, 1218, 1219, 
    1220, 1221, 1222, 1223, 1224, 1225, 1226, 1227, 1228, 1229, 1230, 1231, 1232, 1233, 1234, 1235, 
    1236, 1237, 1238, 1239, 1240, 1241, 1242, 1243, 1244, 1245, 1246, 1247, 1248, 1249, 1250, 1251, 
    1252, 1253, 1254, 1255, 1256, 1257, 1258, 1259, 1260, 1261, 1262, 1263, 1264, 1265, 1266, 1267, 
    1268, 1269, 1270, 1271, 1272, 1273, 1274, 1275, 1276, 1277, 1278, 1279, 1280, 1281, 1282, 1283, 
    1284, 1285, 1286, 1287, 1288, 1289, 1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1298, 1299, 
    1300, 1301, 1302, 1303, 1304, 1305, 1306, 1307, 1308, 1309, 1310, 1311, 1312, 1313, 1314, 1315,
};

void reply_html(Str html) {
    USize escaped_size = 0;
    for (USize i = 0; i < html.len; ++i)
        escaped_size += html_char_size_lut[html.buf[i]] >> 10;
    
    // +8 so that memcpy overwrite is still in-bounds
    if (reply.len + escaped_size + 8 > REPLYSIZE) {
        reply.len += escaped_size;
    } else {
        for (USize i = 0; i < html.len; ++i) {
            char c = html.buf[i];
            U16 size_and_offset = html_char_size_lut[c];
            USize size = size_and_offset >> 10;
            USize offset = size_and_offset & 1023;
            char *new_chars = &html_char_escape_lut[offset];

            // always copying 8 bytes is wayyyyy faster.
            // we set the reply len properly anyways, so it's fine.
            memcpy(&reply.buf[reply.len], new_chars, 8);
            reply.len += size;
        }
    }
}


enum embed_mode {
    EMBED_NONE = 0,
    EMBED_YOUTUBE,
    EMBED_TWITCH_CLIP,
    EMBED_TWITCH_VIDEO,
};

/* 
    metadata format:
    First byte is special:
        low bits  0..4: version (0)
        high bits 4..8: embed mode
    Then follows these null terminated strings:
        title
        link
        embed id (only present if embed mode is nonzero)
        timestamp (only present if embed mode is nonzero)
*/
                
void reply_metadata(char *buf) {
    char version = (*buf) >> 4;
    if (version != 0) {
        reply_push_const("Cannot parse metadata: Version too new.");
        return;
    }
    char embed_mode = (*buf) & 0xf;
    
    char *title = buf + 1;
    USize title_len = strlen(title);
    
    char *link = title + title_len + 1;
    USize link_len = strlen(link);

    reply_push_const("<a class=title href=\"");
    reply_push_slice(link, link_len);
    reply_push_const("\">");
    reply_html((Str) { title, title_len });
    reply_push_const("</a>");
    
    const char *embed = NULL;
    USize embed_len = 0;
    const char *timestamp = NULL;
    USize timestamp_len = 0;
    if (embed_mode != EMBED_NONE) {
        embed = link + link_len + 1;
        embed_len = strlen(embed);
        timestamp = embed + embed_len + 1;
        timestamp_len = strlen(timestamp);
    }
    
    if (embed_mode == EMBED_YOUTUBE) {
        reply_push_const("<iframe class=\"clip-embed\" preload=metadata width=400 height=225 allowfullscreen src=\"https://www.youtube.com/embed/");
        reply_push_slice(embed, embed_len);
        
        if (timestamp_len) {
            reply_push_const("?start=");
            reply_push_slice(timestamp, timestamp_len);
        }
        
        reply_push_const("\"></iframe>");
    }
    else if (embed_mode == EMBED_TWITCH_CLIP) {
        reply_push_const("<iframe class=\"clip-embed\" preload=metadata width=400 height=225 allowfullscreen src=\"https://clips.twitch.tv/embed?clip=");
        reply_push_slice(embed, embed_len);
        
        if (timestamp_len) {
            reply_push_const("&t=");
            reply_push_slice(timestamp, timestamp_len);
        }
        
        reply_push_const("&autoplay=false&parent=melee.cool\"></iframe>");
    }
    else if (embed_mode == EMBED_TWITCH_VIDEO) {
        reply_push_const("<iframe class=\"clip-embed\" preload=metadata width=400 height=225 allowfullscreen src=\"https://player.twitch.tv/?video=v");
        reply_push_slice(embed, embed_len);
        
        if (timestamp_len) {
            reply_push_const("&t=");
            reply_push_slice(timestamp, timestamp_len);
        }
        
        reply_push_const("&autoplay=false&parent=melee.cool\"></iframe>");
    }
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
                char *metadata = entry_metadata(entry);
                reply_metadata(metadata);
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

void reply_clear_error(void) {
    reply_push_const("<div id=err hx-swap-oob=true></div>");
}

void reply_error(const char *err) {
    reply_push_const("<div id=err hx-swap-oob=true>");
    reply_push(err);
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

bool invalid_tag(Str tag) {
    // a..z A..Z 0..9 _
    static const U64 tag_char_ok[4] = { 0x3ff000000000000ULL, 0x7fffffe87fffffeULL, 0, 0, };
    
    U64 ok = 1;
    for (USize i = 0; i < tag.len; ++i) {
        char c = tag.buf[i];
        ok &= tag_char_ok[c >> 6] >> (c & 63);
    }
    
    return (ok & 1) == 0;
}

// https://stackoverflow.com/questions/205923/best-way-to-handle-security-and-avoid-xss-with-user-entered-urls
bool possible_xss_in_link(Str link) {
    // a..z A..Z 0..9 -_+&@#/%?=|~!:,.;()
    static const U64 link_char_ok[4] = { 0xaffffb6a00000000ULL, 0x57fffffe87ffffffULL, 0, 0 };
    
    if (link.len == 0)
        return false;
    
    // ensure http link
    if (link.len < 8)
        return true;

    if (
        memcmp(link.buf, "https://", 8) != 0
        && memcmp(link.buf, "http://", 7) != 0
    ) return true;
    
    // ensure charset ok
    U64 ok = 1;
    for (USize i = 0; i < link.len; ++i) {
        char c = link.buf[i];
        ok &= link_char_ok[c >> 6] >> (c & 63);
    }
    
    return (ok & 1) == 0;
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

/*
twitch embed:
<iframe class="embedIframe__623de" allow="autoplay; fullscreen" frameborder="0" scrolling="no" sandbox="allow-forms allow-modals allow-popups 
    allow-popups-to-escape-sandbox allow-same-origin allow-scripts allow-fullscreen" allowfullscreen="" provider="Twitch" 
    src="https://clips.twitch.tv/embed?clip=ChillyAcceptableFerretStrawBeary-oumAK09a7lKfvCFh&amp;parent=meta.tag" 
    style="position: absolute; top: 0px; left: 0px; max-width: 400px; max-height: 225px;" width="400" height="225"
></iframe>

youtube embed
<iframe class="embedIframe__623de" allow="autoplay; fullscreen" frameborder="0" scrolling="no" sandbox="allow-forms allow-modals allow-popups
    allow-popups-to-escape-sandbox allow-same-origin allow-scripts allow-fullscreen" allowfullscreen="" provider="YouTube"  
    src="https://www.youtube.com/embed/-9Dt2PRBu24?autoplay=1&amp;auto_play=1"
    style="position: absolute; top: 0px; left: 0px; max-width: 400px; max-height: 225px;" width="400" height="225"
></iframe>*/

#define ConstStr(A) (Str) { (char *)A, sizeof(A)-1 }

bool str_contains(Str src, Str find, USize *loc) {
    for (USize i = 0; i + find.len <= src.len; ++i) {
        if (memcmp(src.buf + i, find.buf, find.len) == 0) {
            *loc = i;
            return true; 
        }
    }

    return false;
}

// valid youtube/twitch video id characters:
// a..z | A..Z | 0..9 | - | _
static const U64 id_char_ok[4] = { 0x3ff200000000000ULL, 0x7fffffe87fffffeULL, 0, 0 };

typedef struct EmbedInfo {
    U8 mode;
    Str id;
    Str timestamp;
} EmbedInfo;

// converts links to embeds if recognized.
void convert_link_to_embed(Str link, EmbedInfo *embed) {
    static const Str youtu_be = ConstStr("youtu.be/");
    static const Str youtube_com = ConstStr("youtube.com/watch?v=");
    static const Str twitch_tv = ConstStr("twitch.tv/");
    static const Str twitch_video = ConstStr("/videos/");
    static const Str twitch_clip = ConstStr("/clip/");
    static const Str timestamp_attr_qm = ConstStr("?t=");
    static const Str timestamp_attr_and = ConstStr("&t=");
    
    USize loc;
    
    // determine embed mode
    U8 mode = EMBED_NONE;
    Str id = (Str) { 0 };
    Str timestamp = (Str) { 0 };
    
    if (str_contains(link, youtu_be, &loc)) {
        mode = EMBED_YOUTUBE;
        id = (Str) { link.buf + loc + youtu_be.len, 0 };
    } 
    else if (str_contains(link, youtube_com, &loc)) {
        mode = EMBED_YOUTUBE;
        id = (Str) { link.buf + loc + youtube_com.len, 0 };
    }
    else if (str_contains(link, twitch_tv, &loc)) {
        if (str_contains(link, twitch_clip, &loc)) {
            mode = EMBED_TWITCH_CLIP;
            id = (Str) { link.buf + loc + twitch_clip.len, 0 };
        }
        else if (str_contains(link, twitch_video, &loc)) {
            mode = EMBED_TWITCH_VIDEO;
            id = (Str) { link.buf + loc + twitch_video.len, 0 };
        }
    }
    
    if (mode != EMBED_NONE) {
        // extract id from link
        while (link.buf + link.len > id.buf + id.len) {
            char next_char = id.buf[id.len];
            U64 mask = id_char_ok[next_char >> 6];
            U64 bit = 1ULL << (next_char & 63);
            if (mask & bit) { id.len++; } else { break; }
        }

        if (str_contains(link, timestamp_attr_and, &loc))
            timestamp.buf = link.buf + loc + timestamp_attr_and.len;
        else if (str_contains(link, timestamp_attr_qm, &loc))
            timestamp.buf = link.buf + loc + timestamp_attr_qm.len;
    }
    
    // extract timestamp from link
    if (timestamp.buf) {
        if (mode == EMBED_YOUTUBE) {
            // t=1234s
            // Embed formats don't have the trailing s, so we only parse digits.
            while (link.buf + link.len > timestamp.buf + timestamp.len) {
                char next_char = timestamp.buf[timestamp.len];
    
                // only allow digits
                if (((unsigned char)next_char - (unsigned char)'0') <= 9)
                    timestamp.len++;
                else
                    break;
            }
        }
        else if (mode == EMBED_TWITCH_CLIP || mode == EMBED_TWITCH_VIDEO) {
            // t=1h23m45s
            while (link.buf + link.len > timestamp.buf + timestamp.len) {
                char next_char = timestamp.buf[timestamp.len];
    
                // reusing id characters should be fine
                U64 mask = id_char_ok[next_char >> 6];
                U64 bit = 1ULL << (next_char & 63);
                if (mask & bit) { timestamp.len++; } else { break; }
            }
        }
    }
    
    *embed = (EmbedInfo) { mode, id, timestamp };
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
                
                reply_clear_error();
                reply_send(c);
            }
            else if (mg_match(hm->uri, mg_str("/post-clip"), NULL)) {
                Str title = mg_http_var(hm->body, mg_str("title"));
                Str link = mg_http_var(hm->body, mg_str("link"));
                decode_uri(&title);
                decode_uri(&link);
                if (possible_xss_in_link(link)) {
                    reply_error("Error: Link must start with http:// or https:// and contain only valid URL characters.");
                    reply_send(c);
                    return;
                }
                EmbedInfo embed;
                convert_link_to_embed(link, &embed);
                
                #define METAMAX 2048
                char metabuf[METAMAX];
                USize meta_len = 0;
                
                // copy info byte
                metabuf[meta_len++] = (0 << 4) | embed.mode;
                
                // copy title
                if (meta_len + title.len < METAMAX) {
                    memcpy(metabuf + meta_len, title.buf, title.len);
                    metabuf[meta_len + title.len] = 0;
                }
                meta_len += title.len + 1;
                
                // copy link
                if (meta_len + link.len < METAMAX) {
                    memcpy(metabuf + meta_len, link.buf, link.len);
                    metabuf[meta_len + link.len] = 0;
                }
                meta_len += link.len + 1;
                
                if (embed.mode != EMBED_NONE) {
                    // copy embed id
                    if (meta_len + embed.id.len < METAMAX) {
                        memcpy(metabuf + meta_len, embed.id.buf, embed.id.len);
                        metabuf[meta_len + embed.id.len] = 0;
                    }
                    meta_len += embed.id.len + 1;
                    
                    // copy embed timestamp
                    if (meta_len + embed.timestamp.len < METAMAX) {
                        memcpy(metabuf + meta_len, embed.timestamp.buf, embed.timestamp.len);
                        metabuf[meta_len + embed.timestamp.len] = 0;
                    }
                    meta_len += embed.timestamp.len + 1;
                }
                
                if (meta_len > METAMAX) {
                    reply_error("Error: Title and link are too large!");
                    reply_send(c);
                    return;
                }

                Str metadata = (Str) { metabuf, meta_len };
                EntryIdx entry = create_entry(metadata);
                mg_print_str("- Added entry: \n", metadata);
            
                Str tags_str = mg_http_var(hm->body, mg_str("tags"));
                decode_uri(&tags_str);
                Str tags[TAGMAX];
                USize tag_count = split_tags(tags_str, tags);
                for (USize i = 0; i < tag_count; ++i) {
                    Str tag = tags[i];
                    
                    if (invalid_tag(tag)) {
                        reply_error("Error: Invalid tag. Tags must only contain letters, digits, and underscores.");
                        reply_send(c);
                        return;
                    }

                    mg_print_str("- Tagged: ", tag);
                    tag_entry(entry, tag);
                }
            
                EntrySearch search = { 0 };
                reply_clips(&search, false);
                reply_clear_error();
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
    
    mg_http_listen(&mgr, "http://0.0.0.0:8000", ev_handler, NULL);
    mg_http_listen(&mgr, "http://0.0.0.0:80", ev_handler, NULL);
    mg_http_listen(&mgr, "https://0.0.0.0:443", ev_handler, NULL);

    while (true)
        mg_mgr_poll(&mgr, -1);
    mg_mgr_free(&mgr);
    
    return 0;
}

