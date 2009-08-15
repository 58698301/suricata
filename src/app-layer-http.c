/* Copyright (c) 2009 Victor Julien */

#include "eidps.h"
#include "debug.h"
#include "decode.h"
#include "threads.h"

#include "util-print.h"
#include "util-pool.h"

#include "stream-tcp-private.h"
#include "stream.h"

#include "app-layer-protos.h"
#include "app-layer-parser.h"

#include "util-binsearch.h"
#include "util-unittest.h"

typedef enum {
    HTTP_METHOD_UNKNOWN = 0,
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    /** \todo more.. */
} HttpRequestMethod;

typedef u_int16_t HttpResponseCode;

enum {
    HTTP_FIELD_NONE = 0,

    HTTP_FIELD_REQUEST_LINE,
    HTTP_FIELD_REQUEST_HEADERS,
    HTTP_FIELD_REQUEST_BODY,

    HTTP_FIELD_REQUEST_METHOD,
    HTTP_FIELD_REQUEST_URI,
    HTTP_FIELD_REQUEST_VERSION,

    HTTP_FIELD_RESPONSE_LINE,
    HTTP_FIELD_RESPONSE_HEADERS,
    HTTP_FIELD_RESPONSE_BODY,

    HTTP_FIELD_RESPONSE_VERSION,
    HTTP_FIELD_RESPONSE_CODE,
    HTTP_FIELD_RESPONSE_MSG,

    /* must be last */
    HTTP_FIELD_MAX,
};

typedef struct HttpState_ {
    HttpRequestMethod method;

    HttpResponseCode response_code;
} HttpState;

static int HTTPParseRequestMethod(void *http_state, AppLayerParserState *pstate, u_int8_t *input, u_int32_t input_len, AppLayerParserResult *output) {
    HttpState *hstate = (HttpState *)http_state;

    if (input_len == 4 && memcmp(input, "POST", 4) == 0) {
        //printf("HTTPParseRequestMethod: POST\n");
        hstate->method = HTTP_METHOD_POST;
    } else if (input_len == 3 && memcmp(input, "GET", 3) == 0) {
        //printf("HTTPParseRequestMethod: GET\n");
        hstate->method = HTTP_METHOD_GET;
    }

    return 1;
}

static int HTTPParseResponseCode(void *http_state, AppLayerParserState *pstate, u_int8_t *input, u_int32_t input_len, AppLayerParserResult *output) {
    HttpState *hstate = (HttpState *)http_state;

    if (input_len > 3)
        return 1;

    char code[4] = { 0x0, 0x0, 0x0, 0x0, };
    u_int32_t u = 0;
    for ( ; u < input_len; u++) {
        code[u] = input[u];
    }

    unsigned long ul = strtoul(code, (char **)NULL, 10);
    if (ul >= 1000) { /** \todo what is the max HTTP code */
        return 1;
    }

    hstate->response_code = (HttpResponseCode)ul;
    return 1;
}

static int HTTPParseRequestLine(void *http_state, AppLayerParserState *pstate, u_int8_t *input, u_int32_t input_len, AppLayerParserResult *output) {
    //printf("HTTPParseRequestLine: http_state %p, pstate %p, input %p, input_len %u\n",
    //    http_state, pstate, input, input_len);
    //PrintRawDataFp(stdout, input,input_len);

    u_int16_t max_fields = 3;
    u_int16_t u = 0;
    u_int32_t offset = 0;

    if (pstate == NULL)
        return -1;

    for (u = pstate->parse_field; u < max_fields; u++) {
        //printf("HTTPParseRequestLine: u %u\n", u);

        switch(u) {
            case 0: /* REQUEST METHOD */
            {
                //printf("HTTPParseRequestLine: request method\n");

                const u_int8_t delim[] = { 0x20, };
                int r = AlpParseFieldByDelimiter(output, pstate, HTTP_FIELD_REQUEST_METHOD, delim, sizeof(delim), input, input_len, &offset);
                //printf("HTTPParseRequestLine: r = %d\n", r);

                if (r == 0) {
                    pstate->parse_field = 0;
                    return 0;
                }
                break;
            }
            case 1: /* REQUEST URI */
            {
                const u_int8_t delim[] = { 0x20, };

                u_int8_t *data = input + offset;
                u_int32_t data_len = input_len - offset;

                int r = AlpParseFieldByDelimiter(output, pstate, HTTP_FIELD_REQUEST_URI, delim, sizeof(delim), data, data_len, &offset);
                if (r == 0) {
                    pstate->parse_field = 1;
                    return 0;
                }
                break;
            }
            case 2: /* REQUEST VERSION */
            {
                u_int8_t *data = input + offset;
                u_int32_t data_len = input_len - offset;

                //printf("HTTPParseRequestLine: request version\n");
                //PrintRawDataFp(stdout, data, data_len);

                int r = AlpParseFieldByEOF(output, pstate, HTTP_FIELD_REQUEST_VERSION, data, data_len);
                if (r == 0) {
                    pstate->parse_field = 2;
                    return 0;
                }

                break;
            }
        }
    }

    pstate->parse_field = 0;
    return 1;
}

static int HTTPParseRequest(void *http_state, AppLayerParserState *pstate, u_int8_t *input, u_int32_t input_len, AppLayerParserResult *output) {
    //printf("HTTPParseRequest: http_state %p, state %p, input %p, input_len %u\n",
    //    http_state, pstate, input, input_len);
    //PrintRawDataFp(stdout, input,input_len);

    u_int16_t max_fields = 3;
    u_int16_t u = 0;
    u_int32_t offset = 0;

    if (pstate == NULL)
        return -1;

    //printf("HTTPParseRequest: pstate->parse_field %u\n", pstate->parse_field);

    for (u = pstate->parse_field; u < max_fields; u++) {
        switch(u) {
            case 0: /* REQUEST LINE */
            {
                //printf("HTTPParseRequest: request line (1)\n");
                //PrintRawDataFp(stdout, pstate->store, pstate->store_len);

                const u_int8_t delim[] = { 0x0D, 0x0A };
                int r = AlpParseFieldByDelimiter(output, pstate, HTTP_FIELD_REQUEST_LINE, delim, sizeof(delim), input, input_len, &offset);
                if (r == 0) {
                    pstate->parse_field = 0;
                    //printf("HTTPParseRequest: request line (4)\n");
                    return 0;
                }
                //printf("HTTPParseRequest: request line (2)\n");
                //if (pstate->store_len) PrintRawDataFp(stdout, pstate->store, pstate->store_len);
                //printf("HTTPParseRequest: request line (3)\n");
                break;
            }
            case 1: /* HEADERS */
            {
                //printf("HTTPParseRequest: request headers (offset %u, pstate->store_len %u)\n", offset, pstate->store_len);
                //if (pstate->store_len) PrintRawDataFp(stdout, pstate->store, pstate->store_len);

                const u_int8_t delim[] = { 0x0D, 0x0A, 0x0D, 0x0A };

                u_int8_t *data = input + offset;
                u_int32_t data_len = input_len - offset;

                int r = AlpParseFieldByDelimiter(output, pstate, HTTP_FIELD_REQUEST_HEADERS, delim, sizeof(delim), data, data_len, &offset);
                if (r == 0) {
                    pstate->parse_field = 1;
                    return 0;
                }
                break;
            }
            case 2:
            {
                //printf("HTTPParseRequest: request body\n");

                u_int8_t *data = input + offset;
                u_int32_t data_len = input_len - offset;

                int r = AlpParseFieldByEOF(output, pstate, HTTP_FIELD_REQUEST_BODY, data, data_len);
                if (r == 0) {
                    pstate->parse_field = 2;
                    return 0;
                }

                break;
            }
        }
    }

    pstate->parse_field = 0;
    return 1;
}

static int HTTPParseResponseLine(void *http_state, AppLayerParserState *pstate, u_int8_t *input, u_int32_t input_len, AppLayerParserResult *output) {
    //printf("HTTPParseResponseLine: http_state %p, pstate %p, input %p, input_len %u\n",
    //    http_state, pstate, input, input_len);
    //PrintRawDataFp(stdout, input,input_len);

    u_int16_t max_fields = 3;
    u_int16_t u = 0;
    u_int32_t offset = 0;

    if (pstate == NULL)
        return -1;

    for (u = pstate->parse_field; u < max_fields; u++) {
        //printf("HTTPParseResponseLine: u %u\n", u);

        switch(u) {
            case 0: /* RESPONSE VERSION */
            {
                //printf("HTTPParseResponseLine: request method\n");

                const u_int8_t delim[] = { 0x20, };
                int r = AlpParseFieldByDelimiter(output, pstate, HTTP_FIELD_RESPONSE_VERSION, delim, sizeof(delim), input, input_len, &offset);
                //printf("HTTPParseResponseLine: r = %d\n", r);

                if (r == 0) {
                    pstate->parse_field = 0;
                    return 0;
                }
                break;
            }
            case 1: /* RESPONSE CODE */
            {
                const u_int8_t delim[] = { 0x20, };

                u_int8_t *data = input + offset;
                u_int32_t data_len = input_len - offset;

                int r = AlpParseFieldByDelimiter(output, pstate, HTTP_FIELD_RESPONSE_CODE, delim, sizeof(delim), data, data_len, &offset);
                if (r == 0) {
                    pstate->parse_field = 1;
                    return 0;
                }
                break;
            }
            case 2: /* RESPONSE MSG */
            {
                u_int8_t *data = input + offset;
                u_int32_t data_len = input_len - offset;

                //printf("HTTPParseResponseLine: request version\n");
                //PrintRawDataFp(stdout, data, data_len);

                int r = AlpParseFieldByEOF(output, pstate, HTTP_FIELD_RESPONSE_MSG, data, data_len);
                if (r == 0) {
                    pstate->parse_field = 2;
                    return 0;
                }

                break;
            }
        }
    }

    pstate->parse_field = 0;
    return 1;
}

static int HTTPParseResponse(void *http_state, AppLayerParserState *pstate, u_int8_t *input, u_int32_t input_len, AppLayerParserResult *output) {
    //printf("HTTPParseResponse: http_state %p, pstate %p, input %p, input_len %u\n",
    //    http_state, pstate, input, input_len);

    u_int16_t max_fields = 3;
    u_int16_t u = 0;
    u_int32_t offset = 0;

    if (pstate == NULL)
        return -1;

    //printf("HTTPParseReponse: pstate->parse_field %u\n", pstate->parse_field);

    for (u = pstate->parse_field; u < max_fields; u++) {
        switch(u) {
            case 0: /* RESPONSE LINE */
            {
                //printf("HTTPParseResponse: response line (1)\n");
                //PrintRawDataFp(stdout, pstate->store, pstate->store_len);

                const u_int8_t delim[] = { 0x0D, 0x0A };
                int r = AlpParseFieldByDelimiter(output, pstate, HTTP_FIELD_RESPONSE_LINE, delim, sizeof(delim), input, input_len, &offset);
                if (r == 0) {
                    pstate->parse_field = 0;
                    //printf("HTTPParseResponse: response line (4)\n");
                    return 0;
                }
                //printf("HTTPParseResponse: response line (2)\n");
                //if (pstate->store_len) PrintRawDataFp(stdout, pstate->store, pstate->store_len);
                //printf("HTTPParseResponse: response line (3)\n");
                break;
            }
            case 1: /* HEADERS */
            {
                //printf("HTTPParseResponse: response headers (offset %u, pstate->store_len %u)\n", offset, pstate->store_len);
                //if (pstate->store_len) PrintRawDataFp(stdout, pstate->store, pstate->store_len);

                const u_int8_t delim[] = { 0x0D, 0x0A, 0x0D, 0x0A };

                u_int8_t *data = input + offset;
                u_int32_t data_len = input_len - offset;

                int r = AlpParseFieldByDelimiter(output, pstate, HTTP_FIELD_RESPONSE_HEADERS, delim, sizeof(delim), data, data_len, &offset);
                if (r == 0) {
                    pstate->parse_field = 1;
                    return 0;
                }
                break;
            }
            case 2:
            {
                //printf("HTTPParseResponse: response body\n");

                u_int8_t *data = input + offset;
                u_int32_t data_len = input_len - offset;

                int r = AlpParseFieldByEOF(output, pstate, HTTP_FIELD_RESPONSE_BODY, data, data_len);
                if (r == 0) {
                    pstate->parse_field = 2;
                    return 0;
                }

                break;
            }
        }
    }

    pstate->parse_field = 0;
    return 1;
}

static void *HTTPStateAlloc(void) {
    void *s = malloc(sizeof(HttpState));
    if (s == NULL)
        return NULL;

    memset(s, 0, sizeof(HttpState));
    return s;
}

static void HTTPStateFree(void *s) {
    free(s);
}

void RegisterHTTPParsers(void) {
    AppLayerRegisterProto("http", ALPROTO_HTTP, STREAM_TOSERVER, HTTPParseRequest);
    AppLayerRegisterProto("http", ALPROTO_HTTP, STREAM_TOCLIENT, HTTPParseResponse);

    AppLayerRegisterParser("http.request_line", ALPROTO_HTTP, HTTP_FIELD_REQUEST_LINE, HTTPParseRequestLine, "http");
    AppLayerRegisterParser("http.request.method", ALPROTO_HTTP, HTTP_FIELD_REQUEST_METHOD, HTTPParseRequestMethod, "http.request_line");

    AppLayerRegisterParser("http.response_line", ALPROTO_HTTP, HTTP_FIELD_RESPONSE_LINE, HTTPParseResponseLine, "http");
    AppLayerRegisterParser("http.response.code", ALPROTO_HTTP, HTTP_FIELD_RESPONSE_CODE, HTTPParseResponseCode, "http.response_line");

    AppLayerRegisterStateFuncs(ALPROTO_HTTP, HTTPStateAlloc, HTTPStateFree);
}

/* UNITTESTS */
#ifdef UNITTESTS

/** \test Send a get request in one chunk. */
int HTTPParserTest01(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf[] = "GET / HTTP/1.1\r\nUser-Agent: Victor/1.0\r\n\r\n";
    u_int32_t httplen = sizeof(httpbuf) - 1; /* minus the \0 */
    TcpSession ssn;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());
    f.stream = (void *)&ssn;

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_EOF, httpbuf, httplen);
    if (r != 0) {
        printf("toserver chunk 1 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_GET) {
        printf("expected method %u, got %u: ", HTTP_METHOD_GET, http_state->method);
        result = 0;
        goto end;
    }

end:
    return result;
}

/** \test Send a post request in one chunk. */
int HTTPParserTest02(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf[] = "POST / HTTP/1.1\r\nUser-Agent: Victor/1.0\r\n\r\nPost Data Is c0oL!";
    u_int32_t httplen = sizeof(httpbuf) - 1; /* minus the \0 */
    TcpSession ssn;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());
    f.stream = (void *)&ssn;

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_EOF, httpbuf, httplen);
    if (r != 0) {
        printf("toserver chunk 1 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_POST) {
        printf("expected method %u, got %u: ", HTTP_METHOD_POST, http_state->method);
        result = 0;
        goto end;
    }

end:
    return result;
}

/** \test Send a get request. */
int HTTPParserTest03(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf1[] = "GET / HTTP";
    u_int32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    u_int8_t httpbuf2[] = "/1.1\r\n";
    u_int32_t httplen2 = sizeof(httpbuf2) - 1; /* minus the \0 */
    u_int8_t httpbuf3[] = "User-Agent: Victor/1.0\r\n\r\n";
    u_int32_t httplen3 = sizeof(httpbuf3) - 1; /* minus the \0 */
    TcpSession ssn;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());
    f.stream = (void *)&ssn;

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_START, httpbuf1, httplen1);
    if (r != 0) {
        printf("toserver chunk 1 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf2, httplen2);
    if (r != 0) {
        printf("toserver chunk 2 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_EOF, httpbuf3, httplen3);
    if (r != 0) {
        printf("toserver chunk 3 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_GET) {
        printf("expected method %u, got %u: ", HTTP_METHOD_POST, http_state->method);
        result = 0;
        goto end;
    }

end:
    return result;
}

/** \test Send a get request in 3 chunks, splitting up the request line. */
int HTTPParserTest04(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf1[] = "GET / HTTP";
    u_int32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    u_int8_t httpbuf2[] = "/1.";
    u_int32_t httplen2 = sizeof(httpbuf2) - 1; /* minus the \0 */
    u_int8_t httpbuf3[] = "1\r\nUser-Agent: Victor/1.0\r\n\r\n";
    u_int32_t httplen3 = sizeof(httpbuf3) - 1; /* minus the \0 */
    TcpSession ssn;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());
    f.stream = (void *)&ssn;

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_START, httpbuf1, httplen1);
    if (r != 0) {
        printf("toserver chunk 1 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf2, httplen2);
    if (r != 0) {
        printf("toserver chunk 2 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_EOF, httpbuf3, httplen3);
    if (r != 0) {
        printf("toserver chunk 3 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_GET) {
        printf("expected method %u, got %u: ", HTTP_METHOD_POST, http_state->method);
        result = 0;
        goto end;
    }

end:
    return result;
}

/** \test Send a post request with data, splitting up after the headers. */
int HTTPParserTest05(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf1[] = "POST / HTTP/1.1\r\nUser-Agent: Victor/1.0\r\n\r\n";
    u_int32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    u_int8_t httpbuf2[] = "Post D";
    u_int32_t httplen2 = sizeof(httpbuf2) - 1; /* minus the \0 */
    u_int8_t httpbuf3[] = "ata is c0oL!";
    u_int32_t httplen3 = sizeof(httpbuf3) - 1; /* minus the \0 */
    TcpSession ssn;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());

    f.stream = (void *)&ssn;

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_START, httpbuf1, httplen1);
    if (r != 0) {
        printf("toserver chunk 1 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf2, httplen2);
    if (r != 0) {
        printf("toserver chunk 2 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_EOF, httpbuf3, httplen3);
    if (r != 0) {
        printf("toserver chunk 3 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_POST) {
        printf("expected method %u, got %u: ", HTTP_METHOD_POST, http_state->method);
        result = 0;
        goto end;
    }

end:
    return result;
}

/** \test See how it deals with an incomplete request. */
int HTTPParserTest06(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf1[] = "POST";
    u_int32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    TcpSession ssn;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());

    f.stream = (void *)&ssn;

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_START|STREAM_EOF, httpbuf1, httplen1);
    if (r != 0) {
        printf("toserver chunk 1 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_UNKNOWN) {
        printf("expected method %u, got %u: ", HTTP_METHOD_UNKNOWN, http_state->method);
        result = 0;
        goto end;
    }

end:
    return result;
}

/** \test See how it deals with an incomplete request in multiple chunks. */
int HTTPParserTest07(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf1[] = "PO";
    u_int32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    u_int8_t httpbuf2[] = "ST";
    u_int32_t httplen2 = sizeof(httpbuf2) - 1; /* minus the \0 */
    TcpSession ssn;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());

    f.stream = (void *)&ssn;

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_START, httpbuf1, httplen1);
    if (r != 0) {
        printf("toserver chunk 1 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_EOF, httpbuf2, httplen2);
    if (r != 0) {
        printf("toserver chunk 2 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_UNKNOWN) {
        printf("expected method %u, got %u: ", HTTP_METHOD_UNKNOWN, http_state->method);
        result = 0;
        goto end;
    }

end:
    return result;
}

/** \test Test both sides of a http stream mixed up to see if the parser
 *        properly keeps them separated. */
int HTTPParserTest08(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf1[] = "POST / HTTP/1.1\r\nUser-Agent: Victor/1.0\r\n\r\n";
    u_int32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    u_int8_t httpbuf2[] = "Post D";
    u_int32_t httplen2 = sizeof(httpbuf2) - 1; /* minus the \0 */
    u_int8_t httpbuf3[] = "ata is c0oL!";
    u_int32_t httplen3 = sizeof(httpbuf3) - 1; /* minus the \0 */

    u_int8_t httpbuf4[] = "HTTP/1.1 200 OK\r\nServer: VictorServer/1.0\r\n\r\n";
    u_int32_t httplen4 = sizeof(httpbuf4) - 1; /* minus the \0 */
    u_int8_t httpbuf5[] = "post R";
    u_int32_t httplen5 = sizeof(httpbuf5) - 1; /* minus the \0 */
    u_int8_t httpbuf6[] = "esults are tha bomb!";
    u_int32_t httplen6 = sizeof(httpbuf6) - 1; /* minus the \0 */
    TcpSession ssn;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());

    f.stream = (void *)&ssn;

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_START, httpbuf1, httplen1);
    if (r != 0) {
        printf("toserver chunk 1 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOCLIENT|STREAM_START, httpbuf4, httplen4);
    if (r != 0) {
        printf("toserver chunk 4 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOCLIENT, httpbuf5, httplen5);
    if (r != 0) {
        printf("toserver chunk 5 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf2, httplen2);
    if (r != 0) {
        printf("toserver chunk 2 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER|STREAM_EOF, httpbuf3, httplen3);
    if (r != 0) {
        printf("toserver chunk 3 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOCLIENT|STREAM_EOF, httpbuf6, httplen6);
    if (r != 0) {
        printf("toserver chunk 6 returned %d, expected 0: ", r);
        result = 0;
        goto end;
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_POST) {
        printf("expected method %u, got %u: ", HTTP_METHOD_POST, http_state->method);
        result = 0;
        goto end;
    }

    if (http_state->response_code != 200) {
        printf("expected code %u, got %u: ", http_state->response_code, 200);
        result = 0;
        goto end;
    }
end:
    return result;
}

/** \test Feed the parser our HTTP streams one byte at a time and see how
 *        it copes. */
int HTTPParserTest09(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf1[] = "POST / HTTP/1.1\r\nUser-Agent: Victor/1.0\r\n\r\nPost Data is c0oL!";
    u_int32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    u_int8_t httpbuf2[] = "HTTP/1.1 200 OK\r\nServer: VictorServer/1.0\r\n\r\npost Results are tha bomb!";
    u_int32_t httplen2 = sizeof(httpbuf2) - 1; /* minus the \0 */
    TcpSession ssn;
    int r = 0;
    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());
    f.stream = (void *)&ssn;

    u_int32_t u;
    for (u = 0; u < httplen1; u++) {
        u_int8_t flags = 0;

        if (u == 0) flags = STREAM_TOSERVER|STREAM_START;
        else if (u == (httplen1 - 1)) flags = STREAM_TOSERVER|STREAM_EOF;
        else flags = STREAM_TOSERVER;

        r = AppLayerParse(&f, ALPROTO_HTTP, flags, &httpbuf1[u], 1);
        if (r != 0) {
            printf("toserver chunk %u returned %d, expected 0: ", u, r);
            result = 0;
            goto end;
        }
    }

    for (u = 0; u < httplen2; u++) {
        u_int8_t flags = 0;

        if (u == 0) flags = STREAM_TOCLIENT|STREAM_START;
        else if (u == (httplen2 - 1)) flags = STREAM_TOCLIENT|STREAM_EOF;
        else flags = STREAM_TOCLIENT;

        r = AppLayerParse(&f, ALPROTO_HTTP, flags, &httpbuf2[u], 1);
        if (r != 0) {
            printf("toclient chunk %u returned %d, expected 0: ", u, r);
            result = 0;
            goto end;
        }
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_POST) {
        printf("expected method %u, got %u: ", HTTP_METHOD_POST, http_state->method);
        result = 0;
        goto end;
    }

    if (http_state->response_code != 200) {
        printf("expected code %u, got %u: ", http_state->response_code, 200);
        result = 0;
        goto end;
    }
end:
    return result;
}

/** \test Test case where chunks are smaller than the delim length and the
  *       last chunk is supposed to match the delim. */
int HTTPParserTest10(void) {
    int result = 1;
    Flow f;
    u_int8_t httpbuf1[] = "GET / HTTP/1.0\r\n";
    u_int32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    TcpSession ssn;
    int r = 0;
    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    StreamL7DataPtrInit(&ssn,StreamL7GetStorageSize());
    f.stream = (void *)&ssn;

    u_int32_t u;
    for (u = 0; u < httplen1; u++) {
        u_int8_t flags = 0;

        if (u == 0) flags = STREAM_TOSERVER|STREAM_START;
        else if (u == (httplen1 - 1)) flags = STREAM_TOSERVER|STREAM_EOF;
        else flags = STREAM_TOSERVER;

        r = AppLayerParse(&f, ALPROTO_HTTP, flags, &httpbuf1[u], 1);
        if (r != 0) {
            printf("toserver chunk %u returned %d, expected 0: ", u, r);
            result = 0;
            goto end;
        }
    }

    HttpState *http_state = ssn.l7data[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        printf("no http state: ");
        result = 0;
        goto end;
    }

    if (http_state->method != HTTP_METHOD_GET) {
        printf("expected method %u, got %u: ", HTTP_METHOD_GET, http_state->method);
        result = 0;
        goto end;
    }

end:
    return result;
}

void HTTPParserRegisterTests(void) {
    UtRegisterTest("HTTPParserTest01", HTTPParserTest01, 1);
    UtRegisterTest("HTTPParserTest02", HTTPParserTest02, 1);
    UtRegisterTest("HTTPParserTest03", HTTPParserTest03, 1);
    UtRegisterTest("HTTPParserTest04", HTTPParserTest04, 1);
    UtRegisterTest("HTTPParserTest05", HTTPParserTest05, 1);
    UtRegisterTest("HTTPParserTest06", HTTPParserTest06, 1);
    UtRegisterTest("HTTPParserTest07", HTTPParserTest07, 1);
    UtRegisterTest("HTTPParserTest08", HTTPParserTest08, 1);
    UtRegisterTest("HTTPParserTest09", HTTPParserTest09, 1);
    UtRegisterTest("HTTPParserTest10", HTTPParserTest10, 1);
}

#endif /* UNITTESTS */
