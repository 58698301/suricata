/* Copyright (C) 2007-2010 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Brian Rectanus <brectanu@gmail.com>
 *
 * Implements the http_method keyword
 */

#include "suricata-common.h"
#include "threads.h"
#include "debug.h"
#include "decode.h"
#include "detect.h"

#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "detect-engine-state.h"
#include "detect-content.h"
#include "detect-pcre.h"

#include "flow.h"
#include "flow-var.h"
#include "flow-util.h"

#include "util-debug.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"
#include "util-spm.h"

#include "app-layer.h"

#include <htp/htp.h>
#include "app-layer-htp.h"
#include "detect-http-method.h"
#include "stream-tcp.h"


static int DetectHttpMethodSetup(DetectEngineCtx *, Signature *, char *);
void DetectHttpMethodRegisterTests(void);
void DetectHttpMethodFree(void *);

/**
 * \brief Registration function for keyword: http_method
 */
void DetectHttpMethodRegister(void) {
    sigmatch_table[DETECT_AL_HTTP_METHOD].name = "http_method";
    sigmatch_table[DETECT_AL_HTTP_METHOD].Match = NULL;
    sigmatch_table[DETECT_AL_HTTP_METHOD].AppLayerMatch = NULL;
    sigmatch_table[DETECT_AL_HTTP_METHOD].alproto = ALPROTO_HTTP;
    sigmatch_table[DETECT_AL_HTTP_METHOD].Setup = DetectHttpMethodSetup;
    sigmatch_table[DETECT_AL_HTTP_METHOD].Free  = DetectHttpMethodFree;
    sigmatch_table[DETECT_AL_HTTP_METHOD].RegisterTests = DetectHttpMethodRegisterTests;
    sigmatch_table[DETECT_AL_HTTP_METHOD].flags |= SIGMATCH_PAYLOAD;

    SCLogDebug("registering http_method rule option");
}

/**
 * \brief This function is used to add the parsed "http_method" option
 *        into the current signature.
 *
 * \param de_ctx Pointer to the Detection Engine Context.
 * \param s      Pointer to the Current Signature.
 * \param str    Pointer to the user provided option string.
 *
 * \retval  0 on Success.
 * \retval -1 on Failure.
 */
static int DetectHttpMethodSetup(DetectEngineCtx *de_ctx, Signature *s, char *str)
{
    SCEnter();
    DetectContentData *cd = NULL;

    if ((str != NULL) && (strcmp(str, "") != 0)) {
        SCLogError(SC_ERR_INVALID_ARGUMENT, "http_method does not take an argument");
        SCReturnInt(-1);
    }

    if (s->sm_lists_tail[DETECT_SM_LIST_PMATCH] == NULL) {
        SCLogError(SC_ERR_HTTP_METHOD_NEEDS_PRECEEDING_CONTENT, "http_method "
                "modifies preceeding \"content\", but none was found");
        SCReturnInt(-1);
    }

    SigMatch *sm = DetectContentGetLastPattern(s->sm_lists_tail[DETECT_SM_LIST_PMATCH]);
    if (sm == NULL) {
        SCLogError(SC_ERR_HTTP_METHOD_NEEDS_PRECEEDING_CONTENT, "http_method "
                "modifies preceeding \"content\", but none was found");
        SCReturnInt(-1);
    }

    cd = (DetectContentData *)sm->ctx;

    if (cd->flags & DETECT_CONTENT_RAWBYTES) {
        SCLogError(SC_ERR_HTTP_METHOD_INCOMPATIBLE_WITH_RAWBYTES, "http_method "
                "cannot be used with \"rawbytes\"");
        SCReturnInt(-1);
    }

    if (s->alproto != ALPROTO_UNKNOWN && s->alproto != ALPROTO_HTTP) {
        SCLogError(SC_ERR_CONFLICTING_RULE_KEYWORDS, "rule contains keywords"
                "that conflict with http_method");
        goto error;
    }

    if (cd->flags & DETECT_CONTENT_WITHIN || cd->flags & DETECT_CONTENT_DISTANCE) {
        SigMatch *pm =  SigMatchGetLastSMFromLists(s, 4,
                                                   DETECT_CONTENT, sm->prev,
                                                   DETECT_PCRE, sm->prev);
        if (pm != NULL) {
            /* pm is never NULL.  So no NULL check */
            if (pm->type == DETECT_CONTENT) {
                DetectContentData *tmp_cd = (DetectContentData *)pm->ctx;
                tmp_cd->flags &= ~DETECT_CONTENT_RELATIVE_NEXT;
            } else {
                DetectPcreData *tmp_pd = (DetectPcreData *)pm->ctx;
                tmp_pd->flags &= ~DETECT_PCRE_RELATIVE_NEXT;
            }
        } /* if (pm != NULL) */

        /* please note.  reassigning pm */
        pm = SigMatchGetLastSMFromLists(s, 4,
                                        DETECT_AL_HTTP_METHOD,
                                        s->sm_lists_tail[DETECT_SM_LIST_HMDMATCH],
                                        DETECT_PCRE,
                                        s->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]);
        if (pm == NULL) {
            SCLogError(SC_ERR_HTTP_METHOD_RELATIVE_MISSING, "http_method with "
                    "a distance or within requires preceeding http_method "
                    "content, but none was found");
            goto error;
        }
        if (pm->type == DETECT_PCRE) {
            DetectPcreData *tmp_pd = (DetectPcreData *)pm->ctx;
            tmp_pd->flags |= DETECT_PCRE_RELATIVE_NEXT;
        } else {
            DetectContentData *tmp_cd = (DetectContentData *)pm->ctx;
            tmp_cd->flags |= DETECT_CONTENT_RELATIVE_NEXT;
        }
    }
    cd->id = DetectPatternGetId(de_ctx->mpm_pattern_id_store, cd, DETECT_AL_HTTP_METHOD);
    sm->type = DETECT_AL_HTTP_METHOD;

    /* transfer the sm from the pmatch list to hmdmatch list */
    SigMatchTransferSigMatchAcrossLists(sm,
                                        &s->sm_lists[DETECT_SM_LIST_PMATCH],
                                        &s->sm_lists_tail[DETECT_SM_LIST_PMATCH],
                                        &s->sm_lists[DETECT_SM_LIST_HMDMATCH],
                                        &s->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]);

    /* flag the signature to indicate that we scan the app layer data */
    s->flags |= SIG_FLAG_APPLAYER;
    s->alproto = ALPROTO_HTTP;

    SCReturnInt(0);

error:
    SCReturnInt(-1);
}

/**
 * \brief this function will free memory associated with DetectContentData
 *
 * \param id_d pointer to DetectContentData
 */
void DetectHttpMethodFree(void *ptr) {
    DetectContentData *data = (DetectContentData *)ptr;

    if (data->content != NULL)
        SCFree(data->content);
    SCFree(data);
}

#ifdef UNITTESTS /* UNITTESTS */

#include "stream-tcp-reassemble.h"

/** \test Check a signature with content */
int DetectHttpMethodTest01(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx,
                               "alert tcp any any -> any any "
                               "(msg:\"Testing http_method\"; "
                               "content:\"GET\"; "
                               "http_method; sid:1;)");

    if (de_ctx->sig_list != NULL) {
        result = 1;
    } else {
        printf("sig parse failed: ");
    }

 end:
    if (de_ctx != NULL)
        SigCleanSignatures(de_ctx);
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Check a signature without content (fail) */
int DetectHttpMethodTest02(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx,
                               "alert tcp any any -> any any "
                               "(msg:\"Testing http_method\"; "
                               "http_method; sid:1;)");

    if (de_ctx->sig_list == NULL) {
        result = 1;
    }

 end:
    if (de_ctx != NULL)
        SigCleanSignatures(de_ctx);
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Check a signature with parameter (fail) */
int DetectHttpMethodTest03(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx,
                               "alert tcp any any -> any any "
                               "(msg:\"Testing http_method\"; "
                               "content:\"foobar\"; "
                               "http_method:\"GET\"; sid:1;)");

    if (de_ctx->sig_list == NULL) {
        result = 1;
    }

 end:
    if (de_ctx != NULL)
        SigCleanSignatures(de_ctx);
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Check a signature with fast_pattern (should work) */
int DetectHttpMethodTest04(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx,
                               "alert tcp any any -> any any "
                               "(msg:\"Testing http_method\"; "
                               "content:\"GET\"; "
                               "fast_pattern; "
                               "http_method; sid:1;)");

    if (de_ctx->sig_list != NULL) {
        result = 1;
    }

 end:
    if (de_ctx != NULL)
        SigCleanSignatures(de_ctx);
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Check a signature with rawbytes (fail) */
int DetectHttpMethodTest05(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx,
                               "alert tcp any any -> any any "
                               "(msg:\"Testing http_method\"; "
                               "content:\"GET\"; "
                               "rawbytes; "
                               "http_method; sid:1;)");

    if (de_ctx->sig_list == NULL) {
        result = 1;
    }

 end:
    if (de_ctx != NULL)
        SigCleanSignatures(de_ctx);
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectHttpMethodTest06(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:one; http_method; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("de_ctx->sig_list == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL\n");
        goto end;
    }

    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    DetectContentData *hmd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;
    if (cd->id == hmd->id)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectHttpMethodTest07(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; http_method; content:one; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("de_ctx->sig_list == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL\n");
        goto end;
    }

    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    DetectContentData *hmd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;
    if (cd->id == hmd->id)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectHttpMethodTest08(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:one; content:one; http_method; content:one; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("de_ctx->sig_list == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL\n");
        goto end;
    }

    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    DetectContentData *hmd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;
    if (cd->id != 0 || hmd->id != 1)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectHttpMethodTest09(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; http_method; content:one; content:one; content:one; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("de_ctx->sig_list == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL\n");
        goto end;
    }

    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    DetectContentData *hmd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;
    if (cd->id != 1 || hmd->id != 0)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectHttpMethodTest10(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; http_method; "
                               "content:one; content:one; http_method; content:one; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("de_ctx->sig_list == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL\n");
        goto end;
    }

    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    DetectContentData *hmd1 = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;
    DetectContentData *hmd2 = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->prev->ctx;
    if (cd->id != 1 || hmd1->id != 0 || hmd2->id != 0)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectHttpMethodTest11(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; http_method; "
                               "content:one; content:one; http_method; content:two; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("de_ctx->sig_list == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH] == NULL\n");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL\n");
        goto end;
    }

    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    DetectContentData *hmd1 = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;
    DetectContentData *hmd2 = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->prev->ctx;
    if (cd->id != 2 || hmd1->id != 0 || hmd2->id != 0)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test setting the nocase flag */
static int DetectHttpMethodTest12(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    if (DetectEngineAppendSig(de_ctx, "alert http any any -> any any "
                               "(content:one; http_method; nocase; sid:1;)") == NULL) {
        printf("DetectEngineAppend == NULL: ");
        goto end;
    }
    if (DetectEngineAppendSig(de_ctx, "alert http any any -> any any "
                               "(content:one; nocase; http_method; sid:2;)") == NULL) {
        printf("DetectEngineAppend == NULL: ");
        goto end;
    }

    if (de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL) {
        printf("de_ctx->sig_list->sm_lists[DETECT_SM_LIST_HMDMATCH] == NULL: ");
        goto end;
    }

    DetectContentData *hmd1 = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;
    DetectContentData *hmd2 = de_ctx->sig_list->next->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;

    if (!(hmd1->flags & DETECT_CONTENT_NOCASE)) {
        printf("nocase flag not set on sig 1: ");
        goto end;
    }

    if (!(hmd2->flags & DETECT_CONTENT_NOCASE)) {
        printf("nocase flag not set on sig 2: ");
        goto end;
    }
    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Check a signature with an known request method */
static int DetectHttpMethodSigTest01(void)
{
    int result = 0;
    Flow f;
    uint8_t httpbuf1[] = "GET / HTTP/1.0\r\n"
                         "Host: foo.bar.tld\r\n"
                         "\r\n";
    uint32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    TcpSession ssn;
    Packet *p = NULL;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    HtpState *http_state = NULL;

    memset(&th_v, 0, sizeof(th_v));
    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));

    p = UTHBuildPacket(NULL, 0, IPPROTO_TCP);

    FLOW_INITIALIZE(&f);
    f.protoctx = (void *)&ssn;
    f.src.family = AF_INET;
    f.dst.family = AF_INET;

    p->flow = &f;
    p->flowflags |= FLOW_PKT_TOSERVER;
    p->flowflags |= FLOW_PKT_ESTABLISHED;
    p->flags |= PKT_HAS_FLOW|PKT_STREAM_EST;
    f.alproto = ALPROTO_HTTP;

    StreamTcpInitConfig(TRUE);
    FlowL7DataPtrInit(&f);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,
                                   "alert tcp any any -> any any "
                                   "(msg:\"Testing http_method\"; "
                                   "content:\"GET\"; "
                                   "http_method; sid:1;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,
                          "alert tcp any any -> any any "
                          "(msg:\"Testing http_method\"; "
                          "content:\"POST\"; "
                          "http_method; sid:2;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf1, httplen1);
    if (r != 0) {
        SCLogDebug("toserver chunk 1 returned %" PRId32 ", expected 0: ", r);
        goto end;
    }

    http_state = f.aldata[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        SCLogDebug("no http state: ");
        goto end;
    }

    SigMatchSignatures(&th_v, de_ctx, det_ctx, p);

    if (!(PacketAlertCheck(p, 1))) {
        goto end;
    }
    if (PacketAlertCheck(p, 2)) {
        goto end;
    }

    result = 1;

end:

    if (de_ctx != NULL) SigGroupCleanup(de_ctx);
    if (de_ctx != NULL) SigCleanSignatures(de_ctx);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);

    FlowL7DataPtrFree(&f);
    StreamTcpFreeConfig(TRUE);
    FLOW_DESTROY(&f);
    UTHFreePackets(&p, 1);
    return result;
}

/** \test Check a signature with an unknown request method */
static int DetectHttpMethodSigTest02(void)
{
    int result = 0;
    Flow f;
    uint8_t httpbuf1[] = "FOO / HTTP/1.0\r\n"
                         "Host: foo.bar.tld\r\n"
                         "\r\n";
    uint32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    TcpSession ssn;
    Packet *p = NULL;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    HtpState *http_state = NULL;

    memset(&th_v, 0, sizeof(th_v));
    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));

    p = UTHBuildPacket(NULL, 0, IPPROTO_TCP);

    FLOW_INITIALIZE(&f);
    f.protoctx = (void *)&ssn;
    f.src.family = AF_INET;
    f.dst.family = AF_INET;

    p->flow = &f;
    p->flowflags |= FLOW_PKT_TOSERVER;
    p->flowflags |= FLOW_PKT_ESTABLISHED;
    p->flags |= PKT_HAS_FLOW|PKT_STREAM_EST;
    f.alproto = ALPROTO_HTTP;

    StreamTcpInitConfig(TRUE);
    FlowL7DataPtrInit(&f);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,
                                   "alert tcp any any -> any any "
                                   "(msg:\"Testing http_method\"; "
                                   "content:\"FOO\"; "
                                   "http_method; sid:1;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,
                          "alert tcp any any -> any any "
                          "(msg:\"Testing http_method\"; "
                          "content:\"BAR\"; "
                          "http_method; sid:2;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf1, httplen1);
    if (r != 0) {
        SCLogDebug("toserver chunk 1 returned %" PRId32 ", expected 0: ", r);
        goto end;
    }

    http_state = f.aldata[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        SCLogDebug("no http state: ");
        goto end;
    }

    SigMatchSignatures(&th_v, de_ctx, det_ctx, p);

    if (!(PacketAlertCheck(p, 1))) {
        goto end;
    }
    if (PacketAlertCheck(p, 2)) {
        goto end;
    }

    result = 1;

end:

    if (de_ctx != NULL) SigGroupCleanup(de_ctx);
    if (de_ctx != NULL) SigCleanSignatures(de_ctx);
    if (det_ctx != NULL) DetectEngineThreadCtxDeinit(&th_v, (void *) det_ctx);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);

    FlowL7DataPtrFree(&f);
    StreamTcpFreeConfig(TRUE);
    FLOW_DESTROY(&f);
    UTHFreePackets(&p, 1);
    return result;
}

/** \test Check a signature against an unparsable request */
static int DetectHttpMethodSigTest03(void)
{
    int result = 0;
    Flow f;
    uint8_t httpbuf1[] = " ";
    uint32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    TcpSession ssn;
    Packet *p = NULL;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    HtpState *http_state = NULL;

    memset(&th_v, 0, sizeof(th_v));
    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));

    p = UTHBuildPacket(NULL, 0, IPPROTO_TCP);

    FLOW_INITIALIZE(&f);
    f.protoctx = (void *)&ssn;
    f.src.family = AF_INET;
    f.dst.family = AF_INET;

    p->flow = &f;
    p->flowflags |= FLOW_PKT_TOSERVER;
    p->flowflags |= FLOW_PKT_ESTABLISHED;
    p->flags |= PKT_HAS_FLOW|PKT_STREAM_EST;
    f.alproto = ALPROTO_HTTP;

    StreamTcpInitConfig(TRUE);
    FlowL7DataPtrInit(&f);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,
                                   "alert tcp any any -> any any "
                                   "(msg:\"Testing http_method\"; "
                                   "content:\" \"; "
                                   "http_method; sid:1;)");
    if (s == NULL) {
        SCLogDebug("Bad signature");
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf1, httplen1);
    if (r != 0) {
        SCLogDebug("toserver chunk 1 returned %" PRId32 ", expected 0: ", r);
        goto end;
    }

    http_state = f.aldata[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        SCLogDebug("no http state: ");
        goto end;
    }

    SigMatchSignatures(&th_v, de_ctx, det_ctx, p);

    if (PacketAlertCheck(p, 1)) {
        goto end;
    }

    result = 1;

end:

    if (de_ctx != NULL) SigGroupCleanup(de_ctx);
    if (de_ctx != NULL) SigCleanSignatures(de_ctx);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);

    FlowL7DataPtrFree(&f);
    StreamTcpFreeConfig(TRUE);
    FLOW_DESTROY(&f);
    UTHFreePackets(&p, 1);
    return result;
}

/** \test Check a signature with an request method and negation of the same */
static int DetectHttpMethodSigTest04(void)
{
    int result = 0;
    Flow f;
    uint8_t httpbuf1[] = "GET / HTTP/1.0\r\n"
                         "Host: foo.bar.tld\r\n"
                         "\r\n";
    uint32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    TcpSession ssn;
    Packet *p = NULL;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    HtpState *http_state = NULL;

    memset(&th_v, 0, sizeof(th_v));
    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));

    p = UTHBuildPacket(NULL, 0, IPPROTO_TCP);

    FLOW_INITIALIZE(&f);
    f.protoctx = (void *)&ssn;
    f.src.family = AF_INET;
    f.dst.family = AF_INET;

    p->flow = &f;
    p->flowflags |= FLOW_PKT_TOSERVER;
    p->flowflags |= FLOW_PKT_ESTABLISHED;
    p->flags |= PKT_HAS_FLOW|PKT_STREAM_EST;
    f.alproto = ALPROTO_HTTP;

    StreamTcpInitConfig(TRUE);
    FlowL7DataPtrInit(&f);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,
            "alert tcp any any -> any any (msg:\"Testing http_method\"; "
            "content:\"GET\"; http_method; sid:1;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,
            "alert tcp any any -> any any (msg:\"Testing http_method\"; "
            "content:!\"GET\"; http_method; sid:2;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf1, httplen1);
    if (r != 0) {
        SCLogDebug("toserver chunk 1 returned %" PRId32 ", expected 0: ", r);
        goto end;
    }

    http_state = f.aldata[AlpGetStateIdx(ALPROTO_HTTP)];
    if (http_state == NULL) {
        SCLogDebug("no http state: ");
        goto end;
    }

    SigMatchSignatures(&th_v, de_ctx, det_ctx, p);

    if (!(PacketAlertCheck(p, 1))) {
        printf("sid 1 didn't match but should have: ");
        goto end;
    }
    if (PacketAlertCheck(p, 2)) {
        printf("sid 2 matched but shouldn't have: ");
        goto end;
    }

    result = 1;

end:

    if (de_ctx != NULL) {
        SigGroupCleanup(de_ctx);
        SigCleanSignatures(de_ctx);
    }
    if (det_ctx != NULL) {
        DetectEngineThreadCtxDeinit(&th_v, (void *) det_ctx);
    }
    if (de_ctx != NULL) {
        DetectEngineCtxFree(de_ctx);
    }

    FlowL7DataPtrFree(&f);
    StreamTcpFreeConfig(TRUE);
    FLOW_DESTROY(&f);
    UTHFreePackets(&p, 1);
    return result;
}

#endif /* UNITTESTS */

/**
 * \brief this function registers unit tests for DetectHttpMethod
 */
void DetectHttpMethodRegisterTests(void) {
#ifdef UNITTESTS /* UNITTESTS */
    SCLogDebug("Registering tests for DetectHttpMethod...");
    UtRegisterTest("DetectHttpMethodTest01", DetectHttpMethodTest01, 1);
    UtRegisterTest("DetectHttpMethodTest02", DetectHttpMethodTest02, 1);
    UtRegisterTest("DetectHttpMethodTest03", DetectHttpMethodTest03, 1);
    UtRegisterTest("DetectHttpMethodTest04", DetectHttpMethodTest04, 1);
    UtRegisterTest("DetectHttpMethodTest05", DetectHttpMethodTest05, 1);
    UtRegisterTest("DetectHttpMethodTest06", DetectHttpMethodTest06, 1);
    UtRegisterTest("DetectHttpMethodTest07", DetectHttpMethodTest07, 1);
    UtRegisterTest("DetectHttpMethodTest08", DetectHttpMethodTest08, 1);
    UtRegisterTest("DetectHttpMethodTest09", DetectHttpMethodTest09, 1);
    UtRegisterTest("DetectHttpMethodTest10", DetectHttpMethodTest10, 1);
    UtRegisterTest("DetectHttpMethodTest11", DetectHttpMethodTest11, 1);
    UtRegisterTest("DetectHttpMethodTest12 -- nocase flag", DetectHttpMethodTest12, 1);
    UtRegisterTest("DetectHttpMethodSigTest01", DetectHttpMethodSigTest01, 1);
    UtRegisterTest("DetectHttpMethodSigTest02", DetectHttpMethodSigTest02, 1);
    UtRegisterTest("DetectHttpMethodSigTest03", DetectHttpMethodSigTest03, 1);
    UtRegisterTest("DetectHttpMethodSigTest04", DetectHttpMethodSigTest04, 1);
#endif /* UNITTESTS */
}

