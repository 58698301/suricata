/** Copyright (c) 2009 Open Information Security Foundation
 *
 *  \author Breno Silva <breno.silva@gmail.com>
 */

#include "decode.h"
#include "detect.h"
#include "flow-var.h"

#include "eidps.h"

#include "decode-events.h"
#include "detect-decode-event.h"

#include "util-unittest.h"

#include <pcre.h>
#include <ctype.h>

#define PARSE_REGEX "\\S[0-9A-z_]+[.][A-z+]+$"

static pcre *parse_regex;
static pcre_extra *parse_regex_study;

int DetectDecodeEventMatch (ThreadVars *, PatternMatcherThread *, Packet *, Signature *, SigMatch *);
int DetectDecodeEventSetup (DetectEngineCtx *, Signature *s, SigMatch *m, char *str);
void DecodeEventRegisterTests(void);


/**
 * \brief Registration function for decode-event: keyword
 */
void DetectDecodeEventRegister (void) {
    sigmatch_table[DETECT_DECODE_EVENT].name = "decode-event";
    sigmatch_table[DETECT_DECODE_EVENT].Match = DetectDecodeEventMatch;
    sigmatch_table[DETECT_DECODE_EVENT].Setup = DetectDecodeEventSetup;
    sigmatch_table[DETECT_DECODE_EVENT].Free  = NULL;
    sigmatch_table[DETECT_DECODE_EVENT].RegisterTests = DecodeEventRegisterTests;

    const char *eb;
    int eo;
    int opts = 0;

    parse_regex = pcre_compile(PARSE_REGEX, opts, &eb, &eo, NULL);
    if(parse_regex == NULL)
    {
        printf("pcre compile of \"%s\" failed at offset %d: %s\n", PARSE_REGEX, eo, eb);
        goto error;
    }

    parse_regex_study = pcre_study(parse_regex, 0, &eb);
    if(eb != NULL)
    {
        printf("pcre study failed: %s\n", eb);
        goto error;
    }
    return;

error:
    return;

}

/**
 * \brief This function is used to match decoder event flags set on a packet with those passed via decode-event:
 *
 * \param t pointer to thread vars
 * \param pmt pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param s pointer to the Signature
 * \param m pointer to the sigmatch
 *
 * \retval 0 no match
 * \retval 1 match
 */
int DetectDecodeEventMatch (ThreadVars *t, PatternMatcherThread *pmt, Packet *p, Signature *s, SigMatch *m)
{
    int ret = 0;
    DetectDecodeEventData *de = (DetectDecodeEventData *)m->ctx;

    if(de && DECODER_ISSET_EVENT(p, de->event))
        return 1;

    return ret;
}

/**
 * \brief This function is used to parse decoder events options passed via decode-event: keyword
 *
 * \param rawstr Pointer to the user provided decode-event options
 *
 * \retval de pointer to DetectFlowData on success
 * \retval NULL on failure
 */
DetectDecodeEventData *DetectDecodeEventParse (char *rawstr)
{
    int i;
    DetectDecodeEventData *de = NULL;
#define MAX_SUBSTRINGS 30
    int ret = 0, res = 0, found = 0;
    int ov[MAX_SUBSTRINGS];

    ret = pcre_exec(parse_regex, parse_regex_study, rawstr, strlen(rawstr), 0, 0, ov, MAX_SUBSTRINGS);
    if (ret < 1) {
        goto error;
    }

    const char *str_ptr;
    res = pcre_get_substring((char *)rawstr, ov, MAX_SUBSTRINGS, 0, &str_ptr);

    if (res < 0) {
        goto error;
    }

    for(i = 0; DEvents[i].event_name != NULL; i++)  {
        if((strncasecmp(DEvents[i].event_name,str_ptr,strlen(DEvents[i].event_name))) == 0) {
            found = 1;
            break;
        }
    }

    if(found == 0)
        goto error;

    de = malloc(sizeof(DetectDecodeEventData));
    if (de == NULL) {
        printf("DetectDecodeEventSetup malloc failed\n");
        goto error;
    }

    de->event = DEvents[i].code;
    return de;

error:
    if (de) free(de);
    return NULL;
}

/**
 * \brief this function is used to add the parsed decode-event into the current signature
 *
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param m pointer to the Current SigMatch
 * \param rawstr pointer to the user provided decode-event options
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
int DetectDecodeEventSetup (DetectEngineCtx *de_ctx, Signature *s, SigMatch *m, char *rawstr)
{
    DetectDecodeEventData *de = NULL;
    SigMatch *sm = NULL;

    de = DetectDecodeEventParse(rawstr);
    if (de == NULL)
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_DECODE_EVENT;
    sm->ctx = (void *)de;

    SigMatchAppend(s,m,sm);
    return 0;

error:
    if (de) free(de);
    if (sm) free(sm);
    return -1;
}

/**
 * \brief this function will free memory associated with DetectDecodeEventData
 *
 * \param de pointer to DetectDecodeEventData
 */
void DetectDecodeEventFree(DetectDecodeEventData *de) {
    if(de) free(de);
}

/*
 * ONLY TESTS BELOW THIS COMMENT
 */

/**
 * \test DecodeEventTestParse01 is a test for a  valid decode-event value
 */
int DecodeEventTestParse01 (void) {
    DetectDecodeEventData *de = NULL;
    de = DetectDecodeEventParse("ipv4.pkt_too_small");
    if (de) {
        DetectDecodeEventFree(de);
        return 1;
    }

    return 0;
}


/**
 * \test DecodeEventTestParse02 is a test for a  valid upper + lower case decode-event value
 */
int DecodeEventTestParse02 (void) {
    DetectDecodeEventData *de = NULL;
    de = DetectDecodeEventParse("PPP.pkt_too_small");
    if (de) {
        DetectDecodeEventFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test DecodeEventTestParse03 is a test for a  valid upper case decode-event value
 */
int DecodeEventTestParse03 (void) {
    DetectDecodeEventData *de = NULL;
    de = DetectDecodeEventParse("IPV6.PKT_TOO_SMALL");
    if (de) {
        DetectDecodeEventFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test DecodeEventTestParse04 is a test for an  invalid upper case decode-event value
 */
int DecodeEventTestParse04 (void) {
    DetectDecodeEventData *de = NULL;
    de = DetectDecodeEventParse("IPV6.INVALID_EVENT");
    if (de) {
        DetectDecodeEventFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test DecodeEventTestParse05 is a test for an  invalid char into the decode-event value
 */
int DecodeEventTestParse05 (void) {
    DetectDecodeEventData *de = NULL;
    de = DetectDecodeEventParse("IPV-6,INVALID_CHAR");
    if (de) {
        DetectDecodeEventFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test DecodeEventTestParse06 is a test for match function with valid decode-event value
 */
int DecodeEventTestParse06 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectDecodeEventData *de = NULL;
    SigMatch *sm = NULL;


    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));

    DECODER_SET_EVENT(&p,PPP_PKT_TOO_SMALL);

    de = DetectDecodeEventParse("ppp.pkt_too_small");
    if (de == NULL)
        goto error;

    de->event = PPP_PKT_TOO_SMALL;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_DECODE_EVENT;
    sm->ctx = (void *)de;

    ret = DetectDecodeEventMatch(&tv,NULL,&p,NULL,sm);

    if(ret)
        return 1;

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \brief this function registers unit tests for DecodeEvent
 */
void DecodeEventRegisterTests(void) {
    UtRegisterTest("DecodeEventTestParse01", DecodeEventTestParse01, 1);
    UtRegisterTest("DecodeEventTestParse02", DecodeEventTestParse02, 1);
    UtRegisterTest("DecodeEventTestParse03", DecodeEventTestParse03, 1);
    UtRegisterTest("DecodeEventTestParse04", DecodeEventTestParse04, 0);
    UtRegisterTest("DecodeEventTestParse05", DecodeEventTestParse05, 0);
    UtRegisterTest("DecodeEventTestParse06", DecodeEventTestParse06, 1);
}