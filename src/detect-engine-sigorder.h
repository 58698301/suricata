/** Copyright (c) 2009 Open Information Security Foundation.
 *  \author Anoop Saldanha <poonaatsoc@gmail.com>
 */

#ifndef __DETECT_ENGINE_SIGORDER_H__
#define __DETECT_ENGINE_SIGORDER_H__

/**
 * \brief Different kinds of helper data that can be used by the signature
 *        ordering module.  Used by the "user" field in SCSigSignatureWrapper
 */
typedef enum{
    SC_RADIX_USER_DATA_FLOWBITS,
    SC_RADIX_USER_DATA_FLOWVAR,
    SC_RADIX_USER_DATA_PKTVAR,
    SC_RADIX_USER_DATA_MAX
} SCRadixUserDataType;

/**
 * \brief Signature wrapper used by signature ordering module while ordering
 *        signatures
 */
typedef struct SCSigSignatureWrapper_ {
    /* the wrapped signature */
    Signature *sig;

    /* used as the lower limit SCSigSignatureWrapper that is used by the next
     * ordering function, which will order the incoming Sigwrapper after this
     * (min) wrapper */
    struct SCSigSignatureWrapper_ *min;
    /* used as the upper limit SCSigSignatureWrapper that is used by the next
     * ordering function, which will order the incoming Sigwrapper below this
     * (max) wrapper */
    struct SCSigSignatureWrapper_ *max;

    /* user data that is to be associated with this sigwrapper */
    int **user;

    struct SCSigSignatureWrapper_ *next;
    struct SCSigSignatureWrapper_ *prev;
} SCSigSignatureWrapper;

/**
 * \brief Structure holding the signature ordering function used by the
 *        signature ordering module
 */
typedef struct SCSigOrderFunc_ {
    /* Pointer to the Signature Ordering function */
    void (*FuncPtr)(DetectEngineCtx *, SCSigSignatureWrapper *);

    struct SCSigOrderFunc_ *next;
} SCSigOrderFunc;

void SCSigOrderSignatures(DetectEngineCtx *);
void SCSigRegisterSignatureOrderingFuncs(DetectEngineCtx *);
void SCSigRegisterSignatureOrderingTests(void);
void SCSigSignatureOrderingModuleCleanup(DetectEngineCtx *);

#endif /* __DETECT_ENGINE_SIGORDER_H__ */