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
 * \author Pablo Rincon <pablo.rincon.crespo@gmail.com>
 *
 * Implements isdataat keyword
 */

#include "suricata-common.h"
#include "debug.h"
#include "decode.h"
#include "detect.h"
#include "detect-engine.h"
#include "detect-parse.h"
#include "app-layer.h"

#include "util-unittest.h"
#include "util-unittest-helper.h"

#include "detect-isdataat.h"
#include "detect-content.h"
#include "detect-uricontent.h"

#include "flow.h"
#include "flow-var.h"

#include "util-debug.h"
#include "util-byte.h"
#include "detect-pcre.h"
#include "detect-bytejump.h"

/**
 * \brief Regex for parsing our isdataat options
 */
#define PARSE_REGEX  "^\\s*!?([0-9]{1,5})\\s*(,\\s*relative)?\\s*(,\\s*rawbytes\\s*)?\\s*$"

static pcre *parse_regex;
static pcre_extra *parse_regex_study;

int DetectIsdataatMatch (ThreadVars *, DetectEngineThreadCtx *, Packet *, Signature *, SigMatch *);
int DetectIsdataatSetup (DetectEngineCtx *, Signature *, char *);
void DetectIsdataatRegisterTests(void);
void DetectIsdataatFree(void *);

/**
 * \brief Registration function for isdataat: keyword
 */
void DetectIsdataatRegister (void) {
    sigmatch_table[DETECT_ISDATAAT].name = "isdataat";
    sigmatch_table[DETECT_ISDATAAT].Match = DetectIsdataatMatch;
    sigmatch_table[DETECT_ISDATAAT].Setup = DetectIsdataatSetup;
    sigmatch_table[DETECT_ISDATAAT].Free  = DetectIsdataatFree;
    sigmatch_table[DETECT_ISDATAAT].RegisterTests = DetectIsdataatRegisterTests;

    sigmatch_table[DETECT_ISDATAAT].flags |= SIGMATCH_PAYLOAD;

    const char *eb;
    int eo;
    int opts = 0;

    parse_regex = pcre_compile(PARSE_REGEX, opts, &eb, &eo, NULL);
    if(parse_regex == NULL) {
        SCLogError(SC_ERR_PCRE_COMPILE, "pcre compile of \"%s\" failed at offset %" PRId32 ": %s", PARSE_REGEX, eo, eb);
        goto error;
    }

    parse_regex_study = pcre_study(parse_regex, 0, &eb);
    if(eb != NULL) {
        SCLogError(SC_ERR_PCRE_STUDY, "pcre study failed: %s", eb);
        goto error;
    }
    return;

error:
    /* XXX */
    return;
}

/**
 * \brief This function is used to match isdataat on a packet
 * \todo We need to add support for rawbytes
 *
 * \param t pointer to thread vars
 * \param det_ctx pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param m pointer to the sigmatch that we will cast into DetectIsdataatData
 *
 * \retval 0 no match
 * \retval 1 match
 */
int DetectIsdataatMatch (ThreadVars *t, DetectEngineThreadCtx *det_ctx, Packet *p, Signature *s, SigMatch *m)
{
    DetectIsdataatData *idad = (DetectIsdataatData *)m->ctx;

    SCLogDebug("payload_len: %u , dataat? %u ; relative? %u...", p->payload_len,idad->dataat,idad->flags &ISDATAAT_RELATIVE);

    /* Relative to the last matched content is not performed here, returning match (content should take care of this)*/
    if (idad->flags & ISDATAAT_RELATIVE)
        return 1;

    /* its not relative and we have more data in the packet than the offset of isdataat */
    if (p->payload_len >= idad->dataat) {
        SCLogDebug("matched with payload_len: %u , dataat? %u ; relative? %u...", p->payload_len,idad->dataat,idad->flags &ISDATAAT_RELATIVE);
        return 1;
    }

    return 0;
}

/**
 * \brief This function is used to parse isdataat options passed via isdataat: keyword
 *
 * \param isdataatstr Pointer to the user provided isdataat options
 *
 * \retval idad pointer to DetectIsdataatData on success
 * \retval NULL on failure
 */
DetectIsdataatData *DetectIsdataatParse (char *isdataatstr)
{
    DetectIsdataatData *idad = NULL;
    char *args[3] = {NULL,NULL,NULL};
#define MAX_SUBSTRINGS 30
    int ret = 0, res = 0;
    int ov[MAX_SUBSTRINGS];
    int i=0;

    ret = pcre_exec(parse_regex, parse_regex_study, isdataatstr, strlen(isdataatstr), 0, 0, ov, MAX_SUBSTRINGS);
    if (ret < 1 || ret > 4) {
        SCLogError(SC_ERR_PCRE_MATCH, "pcre_exec parse error, ret %" PRId32 ", string %s", ret, isdataatstr);
        goto error;
    }

    if (ret > 1) {
        const char *str_ptr;
        res = pcre_get_substring((char *)isdataatstr, ov, MAX_SUBSTRINGS, 1, &str_ptr);
        if (res < 0) {
            SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
            goto error;
        }
        args[0] = (char *)str_ptr;


        if (ret > 2) {
            res = pcre_get_substring((char *)isdataatstr, ov, MAX_SUBSTRINGS, 2, &str_ptr);
            if (res < 0) {
                SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
                goto error;
            }
            args[1] = (char *)str_ptr;
        }
        if (ret > 3) {
            res = pcre_get_substring((char *)isdataatstr, ov, MAX_SUBSTRINGS, 3, &str_ptr);
            if (res < 0) {
                SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
                goto error;
            }
            args[2] = (char *)str_ptr;
        }

        idad = SCMalloc(sizeof(DetectIsdataatData));
        if (idad == NULL)
            goto error;

        idad->flags = 0;
        idad->dataat = 0;

        if (args[0] != NULL) {
            if (ByteExtractStringUint16(&idad->dataat, 10,
                strlen(args[0]), args[0]) < 0 ) {
                SCLogError(SC_ERR_INVALID_VALUE, "isdataat out of range");
                SCFree(idad);
                idad = NULL;
                goto error;
            }
        } else {
            goto error;
        }

        if (args[1] !=NULL) {
            idad->flags |= ISDATAAT_RELATIVE;

            if(args[2] !=NULL)
                idad->flags |= ISDATAAT_RAWBYTES;
        }

        if (isdataatstr[0] == '!') {
            idad->flags |= ISDATAAT_NEGATED;
        }

        for (i = 0; i < (ret -1); i++) {
            if (args[i] != NULL)
                SCFree(args[i]);
        }

        return idad;

    }

error:
    for (i = 0; i < (ret -1) && i < 3; i++){
        if (args[i] != NULL)
            SCFree(args[i]);
    }

    if (idad != NULL)
        DetectIsdataatFree(idad);
    return NULL;

}

/**
 * \brief This function is used to add the parsed isdataatdata into the current
 *        signature.
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param isdataatstr pointer to the user provided isdataat options
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
int DetectIsdataatSetup (DetectEngineCtx *de_ctx, Signature *s, char *isdataatstr)
{
    DetectIsdataatData *idad = NULL;
    SigMatch *sm = NULL;
    SigMatch *dm = NULL;
    SigMatch *pm = NULL;
    SigMatch *prev_pm = NULL;

    idad = DetectIsdataatParse(isdataatstr);
    if (idad == NULL)
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_ISDATAAT;
    sm->ctx = (void *)idad;

    if (s->alproto == ALPROTO_DCERPC &&
        idad->flags & ISDATAAT_RELATIVE) {

        pm = SigMatchGetLastSMFromLists(s, 6,
                                        DETECT_CONTENT, s->sm_lists_tail[DETECT_SM_LIST_PMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_PMATCH],
                                        DETECT_BYTEJUMP, s->sm_lists_tail[DETECT_SM_LIST_PMATCH]);
        dm = SigMatchGetLastSMFromLists(s, 6,
                                        DETECT_CONTENT, s->sm_lists_tail[DETECT_SM_LIST_DMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_DMATCH],
                                        DETECT_BYTEJUMP, s->sm_lists_tail[DETECT_SM_LIST_DMATCH]);

        if (pm == NULL) {
            SigMatchAppendDcePayload(s, sm);
        } else if (dm == NULL) {
            SigMatchAppendDcePayload(s, sm);
        } else if (pm->idx > dm->idx) {
            SigMatchAppendPayload(s, sm);
        } else {
            SigMatchAppendDcePayload(s, sm);
        }
        prev_pm = SigMatchGetLastSMFromLists(s, 6,
                                             DETECT_CONTENT, sm->prev,
                                             DETECT_BYTEJUMP, sm->prev,
                                             DETECT_PCRE, sm->prev);
    } else {
        pm = SigMatchGetLastSMFromLists(s, 30,
                                        DETECT_CONTENT, s->sm_lists_tail[DETECT_SM_LIST_PMATCH],
                                        DETECT_URICONTENT, s->sm_lists_tail[DETECT_SM_LIST_UMATCH],
                                        DETECT_AL_HTTP_CLIENT_BODY, s->sm_lists_tail[DETECT_SM_LIST_HCBDMATCH],
                                        DETECT_AL_HTTP_HEADER, s->sm_lists_tail[DETECT_SM_LIST_HHDMATCH],
                                        DETECT_AL_HTTP_RAW_HEADER, s->sm_lists_tail[DETECT_SM_LIST_HRHDMATCH],
                                        DETECT_AL_HTTP_METHOD, s->sm_lists_tail[DETECT_SM_LIST_HMDMATCH],
                                        DETECT_AL_HTTP_COOKIE, s->sm_lists_tail[DETECT_SM_LIST_HCDMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_PMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_UMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_HCBDMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_HHDMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_HRHDMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_HMDMATCH],
                                        DETECT_PCRE, s->sm_lists_tail[DETECT_SM_LIST_HCDMATCH],
                                        DETECT_BYTEJUMP, s->sm_lists_tail[DETECT_SM_LIST_PMATCH]);
        if (pm == NULL) {
            if (idad->flags & ISDATAAT_RELATIVE) {
                SCLogError(SC_ERR_INVALID_SIGNATURE, "isdataat relative seen "
                           "without a previous content uricontent, "
                           "http_client_body, http_header, http_raw_header, "
                           "http_method or http_cookie keyword");
                goto error;
            } else {
                SigMatchAppendSMToList(s, sm, DETECT_SM_LIST_PMATCH);
            }
        } else {
            int list_type;
            if (pm->type == DETECT_PCRE || pm->type == DETECT_BYTEJUMP) {
                list_type = SigMatchListSMBelongsTo(s, pm);
                if (list_type == -1) {
                    goto error;
                }
            } else {
                switch (pm->type) {
                    case DETECT_CONTENT:
                        list_type = DETECT_SM_LIST_PMATCH;
                        break;
                    case DETECT_URICONTENT:
                        list_type = DETECT_SM_LIST_UMATCH;
                        break;
                    case DETECT_AL_HTTP_CLIENT_BODY:
                        list_type = DETECT_SM_LIST_HCBDMATCH;
                        break;
                    case DETECT_AL_HTTP_RAW_HEADER:
                        list_type = DETECT_SM_LIST_HRHDMATCH;
                        break;
                    case DETECT_AL_HTTP_HEADER:
                        list_type = DETECT_SM_LIST_HHDMATCH;
                        break;
                    case DETECT_AL_HTTP_METHOD:
                        list_type = DETECT_SM_LIST_HMDMATCH;
                        break;
                    case DETECT_AL_HTTP_COOKIE:
                        list_type = DETECT_SM_LIST_HCDMATCH;
                        break;
                } /* switch */
            } /* else */

            SigMatchAppendSMToList(s, sm, list_type);
        } /* else - if (pm == NULL) */

        prev_pm = pm;
    }

    if (!(idad->flags & ISDATAAT_RELATIVE)) {
        return 0;
    }

    if (prev_pm == NULL) {
        if (s->alproto == ALPROTO_DCERPC) {
            SCLogDebug("No preceding content or pcre keyword.  Possible "
                       "since this is a dce alproto sig.");
            return 0;
        } else {
            SCLogError(SC_ERR_INVALID_SIGNATURE, "No preceding content, pcre, "
                       "uricontent, http_client_body, http_header, "
                       "http_raw_header, http_method or http_cookie keyword");
            goto error;
        }
    }

    DetectContentData *cd = NULL;
    DetectPcreData *pe = NULL;

    switch (prev_pm->type) {
        case DETECT_CONTENT:
        case DETECT_URICONTENT:
        case DETECT_AL_HTTP_CLIENT_BODY:
        case DETECT_AL_HTTP_HEADER:
        case DETECT_AL_HTTP_RAW_HEADER:
        case DETECT_AL_HTTP_METHOD:
        case DETECT_AL_HTTP_COOKIE:
            /* Set the relative next flag on the prev sigmatch */
            cd = (DetectContentData *)prev_pm->ctx;
            if (cd == NULL) {
                SCLogError(SC_ERR_INVALID_SIGNATURE, "Unknown previous-"
                           "previous keyword!");
                return -1;
            }
            cd->flags |= DETECT_CONTENT_RELATIVE_NEXT;

            break;

        case DETECT_PCRE:
            pe = (DetectPcreData *)prev_pm->ctx;
            if (pe == NULL) {
                SCLogError(SC_ERR_INVALID_SIGNATURE, "Unknown previous-"
                           "previous keyword!");
                return -1;
            }
            pe->flags |= DETECT_PCRE_RELATIVE_NEXT;

            break;

        case DETECT_BYTEJUMP:
            SCLogDebug("Do nothing for bytejump");
            break;

        default:
            /* this will never hit */
            SCLogError(SC_ERR_INVALID_SIGNATURE, "Unknown previous-"
                       "previous keyword!");
            return -1;
    } /* switch */

    return 0;

error:
    //if (idad != NULL)
    //    DetectIsdataatFree(idad);
    //if (sm != NULL)
    //    SCFree(sm);
    return -1;
}

/**
 * \brief this function will free memory associated with DetectIsdataatData
 *
 * \param idad pointer to DetectIsdataatData
 */
void DetectIsdataatFree(void *ptr) {
    DetectIsdataatData *idad = (DetectIsdataatData *)ptr;
    SCFree(idad);
}


#ifdef UNITTESTS

/**
 * \test DetectIsdataatTestParse01 is a test to make sure that we return a correct IsdataatData structure
 *  when given valid isdataat opt
 */
int DetectIsdataatTestParse01 (void) {
    int result = 0;
    DetectIsdataatData *idad = NULL;
    idad = DetectIsdataatParse("30 ");
    if (idad != NULL) {
        DetectIsdataatFree(idad);
        result = 1;
    }

    return result;
}

/**
 * \test DetectIsdataatTestParse02 is a test to make sure that we return a correct IsdataatData structure
 *  when given valid isdataat opt
 */
int DetectIsdataatTestParse02 (void) {
    int result = 0;
    DetectIsdataatData *idad = NULL;
    idad = DetectIsdataatParse("30 , relative");
    if (idad != NULL && idad->flags & ISDATAAT_RELATIVE && !(idad->flags & ISDATAAT_RAWBYTES)) {
        DetectIsdataatFree(idad);
        result = 1;
    }

    return result;
}

/**
 * \test DetectIsdataatTestParse03 is a test to make sure that we return a correct IsdataatData structure
 *  when given valid isdataat opt
 */
int DetectIsdataatTestParse03 (void) {
    int result = 0;
    DetectIsdataatData *idad = NULL;
    idad = DetectIsdataatParse("30,relative, rawbytes ");
    if (idad != NULL && idad->flags & ISDATAAT_RELATIVE && idad->flags & ISDATAAT_RAWBYTES) {
        DetectIsdataatFree(idad);
        result = 1;
    }

    return result;
}

/**
 * \test Test isdataat option for dce sig.
 */
int DetectIsdataatTestParse04(void)
{
    Signature *s = SigAlloc();
    int result = 1;

    s->alproto = ALPROTO_DCERPC;

    result &= (DetectIsdataatSetup(NULL, s, "30") == 0);
    result &= (s->sm_lists[DETECT_SM_LIST_DMATCH] == NULL && s->sm_lists[DETECT_SM_LIST_PMATCH] != NULL);
    SigFree(s);

    s = SigAlloc();
    s->alproto = ALPROTO_DCERPC;
    /* failure since we have no preceding content/pcre/bytejump */
    result &= (DetectIsdataatSetup(NULL, s, "30,relative") == 0);
    result &= (s->sm_lists[DETECT_SM_LIST_DMATCH] != NULL && s->sm_lists[DETECT_SM_LIST_PMATCH] == NULL);

    SigFree(s);

    return result;
}

/**
 * \test Test isdataat option for dce sig.
 */
int DetectIsdataatTestParse05(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 1;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "dce_iface:3919286a-b10c-11d0-9ba8-00c04fd92ef5; "
                               "dce_stub_data; "
                               "content:one; distance:0; "
                               "isdataat:4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        result = 0;
        goto end;
    }
    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_DMATCH] == NULL) {
        result = 0;
        goto end;
    }
    result &= (s->sm_lists_tail[DETECT_SM_LIST_DMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_DMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ) {
        result = 0;
        goto end;
    }

    s->next = SigInit(de_ctx, "alert tcp any any -> any any "
                      "(msg:\"Testing bytejump_body\"; "
                      "dce_iface:3919286a-b10c-11d0-9ba8-00c04fd92ef5; "
                      "dce_stub_data; "
                      "content:one; distance:0; "
                      "isdataat:4,relative; sid:1;)");
    if (s->next == NULL) {
        result = 0;
        goto end;
    }
    s = s->next;
    if (s->sm_lists_tail[DETECT_SM_LIST_DMATCH] == NULL) {
        result = 0;
        goto end;
    }
    result &= (s->sm_lists_tail[DETECT_SM_LIST_DMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_DMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ) {
        result = 0;
        goto end;
    }

    s->next = SigInit(de_ctx, "alert tcp any any -> any any "
                      "(msg:\"Testing bytejump_body\"; "
                      "dce_iface:3919286a-b10c-11d0-9ba8-00c04fd92ef5; "
                      "dce_stub_data; "
                      "content:one; distance:0; "
                      "isdataat:4,relative,rawbytes; sid:1;)");
    if (s->next == NULL) {
        result = 0;
        goto end;
    }
    s = s->next;
    if (s->sm_lists_tail[DETECT_SM_LIST_DMATCH] == NULL) {
        result = 0;
        goto end;
    }
    result &= (s->sm_lists_tail[DETECT_SM_LIST_DMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_DMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         !(data->flags & ISDATAAT_RAWBYTES) ) {
        result = 0;
        goto end;
    }

    s->next = SigInit(de_ctx, "alert tcp any any -> any any "
                      "(msg:\"Testing bytejump_body\"; "
                      "content:one; isdataat:4,relative,rawbytes; sid:1;)");
    if (s->next == NULL) {
        result = 0;
        goto end;
    }
    s = s->next;
    if (s->sm_lists_tail[DETECT_SM_LIST_DMATCH] != NULL) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

int DetectIsdataatTestParse06(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "content:one; "
                               "isdataat:!4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_PMATCH] == NULL) {
        goto end;
    }

    result = 1;

    result &= (s->sm_lists_tail[DETECT_SM_LIST_PMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ||
         !(data->flags & ISDATAAT_NEGATED) ) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

int DetectIsdataatTestParse07(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "uricontent:one; "
                               "isdataat:!4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_UMATCH] == NULL) {
        goto end;
    }

    result = 1;

    result &= (s->sm_lists_tail[DETECT_SM_LIST_UMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_UMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ||
         !(data->flags & ISDATAAT_NEGATED) ) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

int DetectIsdataatTestParse08(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "content:one; http_uri; "
                               "isdataat:!4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_UMATCH] == NULL) {
        goto end;
    }

    result = 1;

    result &= (s->sm_lists_tail[DETECT_SM_LIST_UMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_UMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ||
         !(data->flags & ISDATAAT_NEGATED) ) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

int DetectIsdataatTestParse09(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "content:one; http_client_body; "
                               "isdataat:!4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_HCBDMATCH] == NULL) {
        goto end;
    }

    result = 1;

    result &= (s->sm_lists_tail[DETECT_SM_LIST_HCBDMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_HCBDMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ||
         !(data->flags & ISDATAAT_NEGATED) ) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

int DetectIsdataatTestParse10(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "content:one; http_header; "
                               "isdataat:!4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_HHDMATCH] == NULL) {
        goto end;
    }

    result = 1;

    result &= (s->sm_lists_tail[DETECT_SM_LIST_HHDMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_HHDMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ||
         !(data->flags & ISDATAAT_NEGATED) ) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

int DetectIsdataatTestParse11(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "content:one; http_raw_header; "
                               "isdataat:!4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_HRHDMATCH] == NULL) {
        goto end;
    }

    result = 1;

    result &= (s->sm_lists_tail[DETECT_SM_LIST_HRHDMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_HRHDMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ||
         !(data->flags & ISDATAAT_NEGATED) ) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

int DetectIsdataatTestParse12(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "content:one; http_method; "
                               "isdataat:!4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_HMDMATCH] == NULL) {
        goto end;
    }

    result = 1;

    result &= (s->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_HMDMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ||
         !(data->flags & ISDATAAT_NEGATED) ) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

int DetectIsdataatTestParse13(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    Signature *s = NULL;
    DetectIsdataatData *data = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Testing bytejump_body\"; "
                               "content:one; http_cookie; "
                               "isdataat:!4,relative; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    s = de_ctx->sig_list;
    if (s->sm_lists_tail[DETECT_SM_LIST_HCDMATCH] == NULL) {
        goto end;
    }

    result = 1;

    result &= (s->sm_lists_tail[DETECT_SM_LIST_HCDMATCH]->type == DETECT_ISDATAAT);
    data = (DetectIsdataatData *)s->sm_lists_tail[DETECT_SM_LIST_HCDMATCH]->ctx;
    if ( !(data->flags & ISDATAAT_RELATIVE) ||
         (data->flags & ISDATAAT_RAWBYTES) ||
         !(data->flags & ISDATAAT_NEGATED) ) {
        result = 0;
        goto end;
    }

 end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

/**
 * \test DetectIsdataatTestPacket01 is a test to check matches of
 * isdataat, and isdataat relative
 */
int DetectIsdataatTestPacket01 (void) {
    int result = 0;
    uint8_t *buf = (uint8_t *)"Hi all!";
    uint16_t buflen = strlen((char *)buf);
    Packet *p[3];
    p[0] = UTHBuildPacket((uint8_t *)buf, buflen, IPPROTO_TCP);
    p[1] = UTHBuildPacket((uint8_t *)buf, buflen, IPPROTO_UDP);
    p[2] = UTHBuildPacket((uint8_t *)buf, buflen, IPPROTO_ICMP);

    if (p[0] == NULL || p[1] == NULL ||p[2] == NULL)
        goto end;

    char *sigs[5];
    sigs[0]= "alert ip any any -> any any (msg:\"Testing window 1\"; isdataat:6; sid:1;)";
    sigs[1]= "alert ip any any -> any any (msg:\"Testing window 2\"; content:\"all\"; isdataat:1, relative; isdataat:6; sid:2;)";
    sigs[2]= "alert ip any any -> any any (msg:\"Testing window 3\"; isdataat:8; sid:3;)";
    sigs[3]= "alert ip any any -> any any (msg:\"Testing window 4\"; content:\"Hi\"; isdataat:5, relative; sid:4;)";
    sigs[4]= "alert ip any any -> any any (msg:\"Testing window 4\"; content:\"Hi\"; isdataat:6, relative; sid:5;)";

    uint32_t sid[5] = {1, 2, 3, 4, 5};

    uint32_t results[3][5] = {
                              /* packet 0 match sid 1 but should not match sid 2 */
                              {1, 1, 0, 1, 0},
                              /* packet 1 should not match */
                              {1, 1, 0, 1, 0},
                              /* packet 2 should not match */
                              {1, 1, 0, 1, 0} };

    result = UTHGenericTest(p, 3, sigs, sid, (uint32_t *) results, 5);

    UTHFreePackets(p, 3);
end:
    return result;
}

/**
 * \test DetectIsdataatTestPacket02 is a test to check matches of
 * isdataat, and isdataat relative works if the previous keyword is pcre
 * (bug 144)
 */
int DetectIsdataatTestPacket02 (void) {
    int result = 0;
    uint8_t *buf = (uint8_t *)"GET /AllWorkAndNoPlayMakesWillADullBoy HTTP/1.0"
                    "User-Agent: Wget/1.11.4"
                    "Accept: */*"
                    "Host: www.google.com"
                    "Connection: Keep-Alive"
                    "Date: Mon, 04 Jan 2010 17:29:39 GMT";
    uint16_t buflen = strlen((char *)buf);
    Packet *p;
    p = UTHBuildPacket((uint8_t *)buf, buflen, IPPROTO_TCP);

    if (p == NULL)
        goto end;

    char sig[] = "alert tcp any any -> any any (msg:\"pcre with"
            " isdataat + relative\"; pcre:\"/A(ll|pp)WorkAndNoPlayMakesWillA"
            "DullBoy/\"; isdataat:96,relative; sid:1;)";

    result = UTHPacketMatchSig(p, sig);

    UTHFreePacket(p);
end:
    return result;
}

/**
 * \test DetectIsdataatTestPacket03 is a test to check matches of
 * isdataat, and isdataat relative works if the previous keyword is byte_jump
 * (bug 146)
 */
int DetectIsdataatTestPacket03 (void) {
    int result = 0;
    uint8_t *buf = (uint8_t *)"GET /AllWorkAndNoPlayMakesWillADullBoy HTTP/1.0"
                    "User-Agent: Wget/1.11.4"
                    "Accept: */*"
                    "Host: www.google.com"
                    "Connection: Keep-Alive"
                    "Date: Mon, 04 Jan 2010 17:29:39 GMT";
    uint16_t buflen = strlen((char *)buf);
    Packet *p;
    p = UTHBuildPacket((uint8_t *)buf, buflen, IPPROTO_TCP);

    if (p == NULL)
        goto end;

    char sig[] = "alert tcp any any -> any any (msg:\"byte_jump match = 0 "
    "with distance content HTTP/1. relative against HTTP/1.0\"; byte_jump:1,"
    "46,string,dec; isdataat:87,relative; sid:109; rev:1;)";

    result = UTHPacketMatchSig(p, sig);

    UTHFreePacket(p);
end:
    return result;
}
#endif

/**
 * \brief this function registers unit tests for DetectIsdataat
 */
void DetectIsdataatRegisterTests(void) {
    #ifdef UNITTESTS
    UtRegisterTest("DetectIsdataatTestParse01", DetectIsdataatTestParse01, 1);
    UtRegisterTest("DetectIsdataatTestParse02", DetectIsdataatTestParse02, 1);
    UtRegisterTest("DetectIsdataatTestParse03", DetectIsdataatTestParse03, 1);
    UtRegisterTest("DetectIsdataatTestParse04", DetectIsdataatTestParse04, 1);
    UtRegisterTest("DetectIsdataatTestParse05", DetectIsdataatTestParse05, 1);
    UtRegisterTest("DetectIsdataatTestParse06", DetectIsdataatTestParse06, 1);
    UtRegisterTest("DetectIsdataatTestParse07", DetectIsdataatTestParse07, 1);
    UtRegisterTest("DetectIsdataatTestParse08", DetectIsdataatTestParse08, 1);
    UtRegisterTest("DetectIsdataatTestParse09", DetectIsdataatTestParse09, 1);
    UtRegisterTest("DetectIsdataatTestParse10", DetectIsdataatTestParse10, 1);
    UtRegisterTest("DetectIsdataatTestParse11", DetectIsdataatTestParse11, 1);
    UtRegisterTest("DetectIsdataatTestParse12", DetectIsdataatTestParse12, 1);
    UtRegisterTest("DetectIsdataatTestParse13", DetectIsdataatTestParse13, 1);
    UtRegisterTest("DetectIsdataatTestPacket01", DetectIsdataatTestPacket01, 1);
    UtRegisterTest("DetectIsdataatTestPacket02", DetectIsdataatTestPacket02, 1);
    UtRegisterTest("DetectIsdataatTestPacket03", DetectIsdataatTestPacket03, 1);
    #endif
}
