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
#define ConstStr(A) (Str) { (char *)A, sizeof(A)-1 }

static char msg_headers[1 << 16];
static USize msg_headers_size;
static char msg[1 << 25];
static USize msg_size;
static const char *url = "http://0.0.0.0:8000";

static struct mg_str reply;

static void push_slice(const char *s, USize len) {
    memcpy(msg + msg_size, s, len);
    msg_size += len;
}

static void push_str(const char *s) {
    push_slice(s, strlen(s));
}

static void msg_post(const char *req) {
    msg_size = 0;
    struct mg_str host = mg_url_host(url);
    
    msg_headers_size = mg_snprintf(msg_headers, sizeof(msg_headers),
        "POST %s HTTP/1.1\r\nHost: %.*s\r\n",
        req, (int)host.len, host.buf
    );
}

static void msg_get(const char *req) {
    msg_size = 0;
    struct mg_str host = mg_url_host(url);
    
    msg_headers_size = mg_snprintf(msg_headers, sizeof(msg_headers),
        "GET %s HTTP/1.1\r\nHost: %.*s\r\n",
        req, (int)host.len, host.buf
    );
}

static void msg_var(const char *name, const char *val) {
    push_str(name);
    push_str("=");
    push_str(val);
    push_str("&");
}

static void msg_var_slice(const char *name, Str val) {
    push_str(name);
    push_str("=");
    push_slice(val.buf, val.len);
    push_str("&");
}

static const char *test_name;
static struct mg_mgr mgr;

static void msg_send(struct mg_connection *c) {
    msg_headers[msg_headers_size] = 0;
    
    mg_iobuf_resize(&c->send, 1 << 24);
    memcpy(c->send.buf + c->send.len, msg_headers, msg_headers_size);
    c->send.len += msg_headers_size;
    
    Str length = ConstStr("Content-Length: ");
    memcpy(c->send.buf + c->send.len, length.buf, length.len);
    c->send.len += length.len;
    
    USize n = msg_size;
    char num[32];
    USize num_size = 0;
    do {
        num[num_size++] = '0' + (char)(n % 10);
        n /= 10;
    } while (n);
    for (USize i = 1; i <= num_size; ++i)
        c->send.buf[c->send.len++] = num[num_size - i];
    
    Str newlines = ConstStr("\r\n\r\n");
    memcpy(c->send.buf + c->send.len, newlines.buf, newlines.len);
    c->send.len += newlines.len;
    
    memcpy(c->send.buf + c->send.len, msg, msg_size);
    c->send.len += msg_size;
    
    mg_mgr_poll(&mgr, -1);
    mg_mgr_poll(&mgr, -1);
    mg_mgr_poll(&mgr, 1);
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    (void)c;
    if (ev == MG_EV_ERROR) {
        printf("Client error: %s\n", (char *) ev_data);
    }
    else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        free(reply.buf);
        reply = mg_strdup(hm->message);
        
        bool err = mg_http_status(hm) != 200;
        if (err) {
            printf("REPLY:\n%.*s\n", (int) reply.len, reply.buf);
            printf("test '%s' failed: non-200 status code\n", test_name);
            exit(1);
        }
    }
}

void expect_reply_contains(const char *substr) {
    Str s = { (char*)substr, strlen(substr) };
    for (USize i = 0; i+s.len < reply.len; ++i) {
        if (memcmp(s.buf, reply.buf+i, s.len) == 0)
            return;
    }
    
    printf("REPLY:\n%.*s\n", (int) reply.len, reply.buf);
    printf("test '%s' failed: did not contain substring '%s'\n", test_name, substr);
    exit(1);
}

void expect_reply_err(bool should_err) {
    Str id_err = ConstStr("<div id=err hx-swap-oob=true>");
    bool is_err = false;
    for (USize i = 0; i+id_err.len < reply.len; ++i) {
        if (memcmp(id_err.buf, reply.buf+i, id_err.len) == 0) {
            // ensure not clearing error
            Str id_err_clear = ConstStr("<div id=err hx-swap-oob=true></div>");
            if (memcmp(id_err_clear.buf, reply.buf+i, id_err_clear.len) == 0)
                is_err = false;
            else
                is_err = true;
        }
    }
    
    if (is_err && !should_err) {
        printf("REPLY:\n%.*s\n", (int) reply.len, reply.buf);
        printf("test '%s' failed: returned error\n", test_name);
        exit(1);
    }
    
    if (!is_err && should_err) {
        printf("REPLY:\n%.*s\n", (int) reply.len, reply.buf);
        printf("test '%s' failed: did not return error\n", test_name);
        exit(1);
    }
}

void start_test(const char *name) {
    test_name = name;
    printf("test '%s':\n", name);
}

void test(void) {
    reply.buf = malloc(1 << 24);
    char *buf = malloc(1 << 24);
    USize bufsize = 0;

    usleep(10000);
    mg_log_set(MG_LL_ERROR);
    mg_mgr_init(&mgr);
    struct mg_connection *c = mg_http_connect(&mgr, url, ev_handler, NULL);
    
    printf("test");

    // GET HTML TEST ------------------------------------------
    
    start_test("get clips html");
    msg_get("/clips/");
    msg_send(c);
    
    start_test("get header");
    msg_get("/header.js");
    msg_send(c);
    
    start_test("get htmx");
    msg_get("/htmx.js");
    msg_send(c);
    
    // POST CLIPS ERROR TESTS ------------------------------------------
    
    start_test("post clip no vars");
    msg_post("/post-clip");
    msg_send(c);
    expect_reply_err(true);

    start_test("post clip bad title");
    msg_post("/post-clip");
    msg_var("title", "");
    msg_var("link", "https://google.com");
    msg_var("tags", "tag1");
    msg_send(c);
    expect_reply_err(true);

    start_test("post clip bad link");
    msg_post("/post-clip");
    msg_var("title", "title");
    msg_var("link", "<script>alert(1);</script>");
    msg_var("tags", "tag1");
    msg_send(c);
    expect_reply_err(true);
    
    start_test("post clip no tags");
    msg_post("/post-clip");
    msg_var("title", "title");
    msg_var("link", "https://google.com");
    msg_var("tags", "");
    msg_send(c);
    expect_reply_err(true);
    
    start_test("post clip bad tags");
    msg_post("/post-clip");
    msg_var("title", "title");
    msg_var("link", "https://google.com");
    msg_var("tags", "!all");
    msg_send(c);
    expect_reply_err(true);
    
    start_test("post clip many tags");
    msg_post("/post-clip");
    msg_var("title", "title");
    msg_var("link", "https://google.com");
    {
        bufsize = 0;
        for (USize i = 0; i < 1024; ++i) {
            buf[bufsize++] = 'a';
            buf[bufsize++] = ' ';
        }
        buf[bufsize] = 0;
    }
    msg_var("tags", buf);
    msg_send(c);
    expect_reply_err(true);

    start_test("post clip large tag");
    msg_post("/post-clip");
    msg_var("title", "title");
    msg_var("link", "https://google.com");
    {
        bufsize = 0;
        for (USize i = 0; i < 1024; ++i)
            buf[bufsize++] = 'a';
        buf[bufsize] = 0;
    }
    msg_var("tags", buf);
    msg_send(c);
    expect_reply_err(true);
    
    start_test("post clip large metadata");
    msg_post("/post-clip");
    {
        bufsize = 0;
        for (USize i = 0; i < 4096; ++i)
            buf[bufsize++] = 'a';
        buf[bufsize] = 0;
    }
    msg_var("title", buf);
    msg_var("link", "https://google.com");
    msg_var("tags", "1");
    msg_send(c);
    expect_reply_err(true);
    
    // FIND CLIPS ERROR TESTS ------------------------------------------
    
    start_test("find clips too many tags");
    msg_post("/find-clips");
    {
        bufsize = 0;
        for (USize i = 0; i < 1024; ++i) {
            buf[bufsize++] = 'a';
            buf[bufsize++] = ' ';
        }
        buf[bufsize] = 0;
    }
    msg_var("tags", buf);
    msg_send(c);
    expect_reply_err(true);
    
    start_test("find clips large tag");
    msg_post("/find-clips");
    {
        bufsize = 0;
        for (USize i = 0; i < 1024; ++i) {
            buf[bufsize++] = 'a';
        }
        buf[bufsize] = 0;
    }
    msg_var("tags", buf);
    msg_send(c);
    expect_reply_err(true);
    
    start_test("find clips bad tags");
    msg_post("/find-clips");
    msg_var("tags", "!all");
    msg_send(c);
    expect_reply_err(true);
    
    start_test("find clips many large tags");
    msg_post("/find-clips");
    {
        bufsize = 0;
        for (USize i = 0; i < 1000; ++i) {
            for (USize j = 0; j < 1000; ++j)
                buf[bufsize++] = 'a';
            buf[bufsize++] = ' ';
        }
        buf[bufsize] = 0;
    }
    msg_var("tags", buf);
    msg_send(c);
    expect_reply_err(true);
    
    // EMPTY FIND CLIPS TESTS ------------------------------------------
    
    start_test("find most recent clips no results");
    msg_post("/find-clips");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("find clips tagged a b c no results");
    msg_post("/find-clips");
    msg_var("tags", "a b c");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    // BASIC POST CLIPS TESTS ------------------------------------------
    
    start_test("basic post clip");
    msg_post("/post-clip");
    msg_var("title", "title");
    msg_var("link", "https://google.com");
    msg_var("tags", "1 2 3 4");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    // BASIC FIND CLIPS TESTS ------------------------------------------
    
    start_test("basic find clips");
    msg_post("/find-clips");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips by tag 1");
    msg_post("/find-clips");
    msg_var("tags", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips by tag 4");
    msg_post("/find-clips");
    msg_var("tags", "4");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips by tag 1 4");
    msg_post("/find-clips");
    msg_var("tags", "1 4");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips by tag 1 2 3 4");
    msg_post("/find-clips");
    msg_var("tags", "1 2 3 4");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips by tag n/a");
    msg_post("/find-clips");
    msg_var("tags", "na");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("basic find clips truncate indices");
    msg_post("/find-clips");
    msg_var("idx", "----------------------------------------------------------------------------------------------------------------------------------------------------");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    // ADVANCED FIND CLIPS TESTS ------------------------------------------
    
    start_test("basic find clips by no tag explicit idx");
    msg_post("/find-clips");
    msg_var("idx", "0");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips by no tag oob close");
    msg_post("/find-clips");
    msg_var("idx", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("basic find clips by no tag oob far");
    msg_post("/find-clips");
    msg_var("idx", "FFFFFFFF");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("basic find clips by no tag oob");
    msg_post("/find-clips");
    msg_var("idx", "FFFFFFFF");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("basic find clips by tag 1 2 3 4 explicit idx");
    msg_post("/find-clips");
    msg_var("tags", "1 2 3 4");
    msg_var("idx", "0-0-0-0");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips by tag 1 2 3 4 oob close");
    msg_post("/find-clips");
    msg_var("tags", "1 2 3 4");
    msg_var("idx", "1-1-1-1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("basic find clips by tag 1 2 3 4 oob far");
    msg_post("/find-clips");
    msg_var("tags", "1 2 3 4");
    msg_var("idx", "FFFFFFFF-FFFFFFFF-FFFFFFFF-FFFFFFFF");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("basic find clips by tag 1 2 3 4 oob");
    msg_post("/find-clips");
    msg_var("tags", "1 2 3 4");
    msg_var("idx", "FFFFFFFF-FFFFFFFF-FFFFFFFF-FFFFFFFF");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    
    start_test("basic find clips rev by no tag explicit idx");
    msg_post("/find-clips");
    msg_var("idx", "0");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("basic find clips rev by no tag oob close");
    msg_post("/find-clips");
    msg_var("idx", "2");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips rev by no tag oob far");
    msg_post("/find-clips");
    msg_var("idx", "FFFFFFFF");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips rev by tag 1 2 3 4 explicit idx");
    msg_post("/find-clips");
    msg_var("tags", "1 2 3 4");
    msg_var("idx", "0-0-0-0");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("basic find clips rev by tag 1 2 3 4 oob close");
    msg_post("/find-clips");
    msg_var("tags", "1 2 3 4");
    msg_var("idx", "1-1-1-1");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("basic find clips rev by tag 1 2 3 4 oob far");
    msg_post("/find-clips");
    msg_var("tags", "1 2 3 4");
    msg_var("idx", "FFFFFFFF-FFFFFFFF-FFFFFFFF-FFFFFFFF");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    // POPULATE
    
    msg_post("/populate");
    msg_send(c);
    expect_reply_err(false);
    
    // POPULATED ADVANCED FIND CLIPS TESTS ------------------------------------------
    
    start_test("populated find clips by no tag explicit idx");
    msg_post("/find-clips");
    msg_var("idx", "0");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("populated find clips by no tag mid");
    msg_post("/find-clips");
    msg_var("idx", "100");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("populated find clips by no tag oob close");
    msg_post("/find-clips");
    msg_var("idx", "10000");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("populated find clips by no tag oob far");
    msg_post("/find-clips");
    msg_var("idx", "FFFFFFFF");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("populated find clips by no tag oob");
    msg_post("/find-clips");
    msg_var("idx", "FFFFFFFF");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("populated find clips by tag abc cba explicit idx");
    msg_post("/find-clips");
    msg_var("tags", "abc cba");
    msg_var("idx", "0-0");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("populated find clips by tag abc cba oob close");
    msg_post("/find-clips");
    msg_var("tags", "abc cba");
    msg_var("idx", "100-100");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("populated find clips by tag abc cba oob far");
    msg_post("/find-clips");
    msg_var("tags", "abc cba");
    msg_var("idx", "FFFFFFFF-FFFFFFFF");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("populated find clips by tag abc cba oob");
    msg_post("/find-clips");
    msg_var("tags", "abc cba");
    msg_var("idx", "FFFFFFFF-FFFFFFFF");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("populated find clips rev by no tag explicit idx");
    msg_post("/find-clips");
    msg_var("idx", "0");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("populated find clips by no tag mid");
    msg_post("/find-clips");
    msg_var("idx", "100");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("populated find clips rev by no tag oob close");
    msg_post("/find-clips");
    msg_var("idx", "2");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("populated find clips rev by no tag oob far");
    msg_post("/find-clips");
    msg_var("idx", "FFFFFFFF");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("populated find clips rev by tag abc cba explicit idx");
    msg_post("/find-clips");
    msg_var("tags", "abc cba");
    msg_var("idx", "0-0");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("No results");
    
    start_test("populated find clips rev by tag abc cba oob close");
    msg_post("/find-clips");
    msg_var("tags", "abc cba");
    msg_var("idx", "100-100");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("populated find clips rev by tag abc cba oob far");
    msg_post("/find-clips");
    msg_var("tags", "abc cba");
    msg_var("idx", "FFFFFFFF-FFFFFFFF");
    msg_var("rev", "1");
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    // NULL BYTE TESTS ------------------------------------
    
    Str null_title = ConstStr("a--\0--b");
    Str null_link = ConstStr("https://goo\0gle.com");
    Str null_tag = ConstStr("ab\0c");
    
    start_test("post clip with null title");
    msg_post("/post-clip");
    msg_var_slice("title", null_title);
    msg_var("link", "https://google.com");
    msg_var("tags", "1 2 3 4");
    msg_send(c);
    expect_reply_err(true);
    
    start_test("post clip with null link");
    msg_post("/post-clip");
    msg_var("title", "title");
    msg_var_slice("link", null_link);
    msg_var("tags", "1 2 3 4");
    msg_send(c);
    expect_reply_err(true);
    
    start_test("post clip with null tag");
    msg_post("/post-clip");
    msg_var("link", "https://google.com");
    msg_var("title", "title");
    msg_var_slice("tags", null_tag);
    msg_send(c);
    expect_reply_err(true);
    
    start_test("find clips null idx");
    msg_post("/find-clips");
    msg_var_slice("idx", (Str) { (char*)"1\02", 3 });
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("find clips null rev");
    msg_post("/find-clips");
    msg_var("idx", "10");
    msg_var_slice("rev", (Str) { (char*)"\0", 1 });
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("find clips null tag");
    msg_post("/find-clips");
    msg_var("idx", "10");
    msg_var_slice("tags", null_tag);
    msg_send(c);
    expect_reply_err(true);
    
    // QUOTE TESTS ------------------------------------
    
    Str quote_title = ConstStr("a--\"--b");
    Str quote_link = ConstStr("https://goo\"gle.com");
    Str quote_tag = ConstStr("ab\"c");
    
    start_test("post clip with quote title");
    msg_post("/post-clip");
    msg_var_slice("title", quote_title);
    msg_var("link", "https://google.com");
    msg_var("tags", "1 2 3 4");
    msg_send(c);
    expect_reply_err(false);
    
    start_test("post clip with quote link");
    msg_post("/post-clip");
    msg_var("title", "title");
    msg_var_slice("link", quote_link);
    msg_var("tags", "1 2 3 4");
    msg_send(c);
    expect_reply_err(true);
    
    start_test("post clip with quote tag");
    msg_post("/post-clip");
    msg_var("link", "https://google.com");
    msg_var("title", "title");
    msg_var_slice("tags", quote_tag);
    msg_send(c);
    expect_reply_err(true);
    
    start_test("find clips quote idx");
    msg_post("/find-clips");
    msg_var_slice("idx", (Str) { (char*)"1\"2", 3 });
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("find clips quote rev");
    msg_post("/find-clips");
    msg_var("idx", "10");
    msg_var_slice("rev", (Str) { (char*)"\"", 1 });
    msg_send(c);
    expect_reply_err(false);
    expect_reply_contains("class=clip");
    
    start_test("find clips quote tag");
    msg_post("/find-clips");
    msg_var("idx", "10");
    msg_var_slice("tags", quote_tag);
    msg_send(c);
    expect_reply_err(true);
    
    // FINISHED! ------------------------------------------
    
    printf("## Tests all good! ##\n");
}
