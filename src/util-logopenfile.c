/* vi: set et ts=4:
 */
#include <sys/socket.h>
#include <sys/un.h>

#include "suricata-common.h" /* errno.h, string.h, etc. */
#include "tm-modules.h"      /* LogFileCtx */
#include "conf.h"            /* ConfNode, etc. */
#include "output.h"          /* DEFAULT_LOG_* */

/** \brief connect to the indicated local stream socket, logging any errors
 *  \param path filesystem path to connect to
 *  \retval FILE* on success (fdopen'd wrapper of underlying socket)
 *  \retval NULL on error
 */
FILE * SCLogOpenSocketFp(const char *path)
{
    struct sockaddr_un sun;
    FILE * ret;
    int s = -1;

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s < 0) goto err;

    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, path, sizeof(sun.sun_path) - 1);

    if (connect(s, (const struct sockaddr *)&sun, sizeof(sun)) < 0)
        goto err;

    ret = fdopen(s, "w");
    if (ret == NULL)
        goto err;

    return ret;

err:
    SCLogError(SC_ERR_SOCKET, "Error connecting to socket \"%s\": %s",
               path, strerror(errno));

    if (s >= 0)
        close(s);

    return NULL;
}

/** \brief open the indicated file, logging any errors
 *  \param path filesystem path to open
 *  \param append_setting open file with O_APPEND: "yes" or "no"
 *  \retval FILE* on success
 *  \retval NULL on error
 */
FILE * SCLogOpenFileFp(const char *path, const char *append_setting)
{
    FILE *ret = NULL;

    if (strcasecmp(append_setting, "yes") == 0) {
        ret = fopen(path, "a");
    } else {
        ret = fopen(path, "w");
    }

    if (ret == NULL)
        SCLogError(SC_ERR_FOPEN, "Error opening file: \"%s\": %s",
                   path, strerror(errno));
    return ret;
}

/** \brief open a generic output "log file", which may be a regular file or a socket
 *  \param conf ConfNode structure for the output section in question
 *  \param log_ctx Log file context allocated by caller
 *  \param default_filename Default name of file to open, if not specified in ConfNode
 *  \retval 0 on success
 *  \retval -1 on error
 */
int SCConfLogOpenGeneric(ConfNode *conf,
                         LogFileCtx *log_ctx,
                         const char *default_filename)
{
    char log_path[PATH_MAX];
    char *log_dir;
    const char *filename, *filetype;

    // Arg check
    if (conf == NULL || log_ctx == NULL || default_filename == NULL) {
        SCLogError(SC_ERR_INVALID_ARGUMENT,
                   "SCConfLogOpenGeneric(conf %p, ctx %p, default %p) "
                   "missing an argument",
                   conf, log_ctx, default_filename);
        return -1;
    }
    if (log_ctx->fp != NULL) {
        SCLogError(SC_ERR_INVALID_ARGUMENT,
                   "SCConfLogOpenGeneric: previously initialized Log CTX "
                   "encountered");
        return -1;
    }

    // Resolve the given config
    filename = ConfNodeLookupChildValue(conf, "filename");
    if (filename == NULL)
        filename = default_filename;

    if (ConfGet("default-log-dir", &log_dir) != 1)
        log_dir = DEFAULT_LOG_DIR;

    snprintf(log_path, PATH_MAX, "%s/%s", log_dir, filename);

    filetype = ConfNodeLookupChildValue(conf, "type");
    if (filetype == NULL)
        filetype = DEFAULT_LOG_FILETYPE;

    // Now, what have we been asked to open?
    if (strcasecmp(filetype, "socket") == 0) {
        log_ctx->fp = SCLogOpenSocketFp(log_path);
    } else if (strcasecmp(filetype, DEFAULT_LOG_FILETYPE) == 0) {
        const char *append;

        append = ConfNodeLookupChildValue(conf, "append");
        if (append == NULL)
            append = DEFAULT_LOG_MODE_APPEND;
        log_ctx->fp = SCLogOpenFileFp(log_path, append);
    } else {
        SCLogError(SC_ERR_INVALID_YAML_CONF_ENTRY, "Invalid entry for "
                   "%s.type.  Expected \"regular\" (default) or \"socket\"",
                   conf->name);
    }

    if (log_ctx->fp == NULL)
        return -1; // Error already logged by Open...Fp routine

    SCLogInfo("%s output initialized: %s", conf->name, filename);

    return 0;
}
