/* TN5250 - An implementation of the 5250 telnet protocol.
 * Copyright (C) 1997-2008 Michael Madore
 *
 * This file is part of TN5250.
 *
 * TN5250 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1, or (at your option)
 * any later version.
 *
 * TN5250 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA
 *
 */
#include "tn5250-private.h"

#if defined(__SVR4) && defined(__sun)
#include <sys/filio.h>
#endif

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_LIBSSL
#include <openssl/err.h>
#endif

static unsigned char mapfix[256];
static unsigned char mapfix2[256];
static unsigned char mapfix3[256];
static unsigned char mapfix4[256];

#ifndef _WIN32

/****f* lp5250d/tn5250_closeall
 * NAME
 *    tn5250_closeall
 * SYNOPSIS
 *    tn5250_closeall (fd);
 * INPUTS
 *    int fd	- The starting file descriptor.
 * DESCRIPTION
 *    Closes all file descriptors >= a specified value.
 *****/
void tn5250_closeall(int fd) {
    int fdlimit = sysconf(_SC_OPEN_MAX);

    while (fd < fdlimit) {
        close(fd++);
    }
}

/*
  Signal handler for SIGCHLD.  We use waitpid instead of wait, since there
  is no way to tell wait not to block if there are still non-terminated child
  processes.
*/
void sig_child(int signum) {
    int pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        ;

    return;
}

/****f* lp5250d/tn5250_daemon
 * NAME
 *    tn5250_daemon
 * SYNOPSIS
 *    ret = tn5250_daemon (nochdir, noclose);
 * INPUTS
 *    int nochdir	- 0 to perform chdir.
 *    int noclose	- 0 to close all file handles.
 * DESCRIPTION
 *    Detach process from user and disappear into the background
 *    returns -1 on failure, but you can't do much except exit in that
 *    case since we may already have forked. Believed to work on all
 *    Posix systems.
 *****/
int tn5250_daemon(int nochdir, int noclose, int ignsigcld) {
    struct sigaction sa;

    switch (fork()) {
    case 0:
        break;
    case -1:
        return -1;
    default:
        _exit(0); /* exit the original process */
    }

    if (setsid() < 0) { /* shoudn't fail */
        return -1;
    }

    /* dyke out this switch if you want to acquire a control tty in */
    /* the future -- not normally advisable for daemons */

    switch (fork()) {
    case 0:
        break;
    case -1:
        return -1;
    default:
        _exit(0);
    }

    if (!nochdir) {
        chdir("/");
    }

    if (!noclose) {
        tn5250_closeall(0);
        open("/dev/null", O_RDWR);
        dup(0);
        dup(0);
    }

    umask(0);

    if (ignsigcld) {
        sa.sa_handler = sig_child;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;

#ifdef SIGCHLD
        sigaction(SIGCHLD, &sa, NULL);
#else
#ifdef SIGCLD
        sigaction(SIGCLD, &sa, NULL);
#endif
#endif
    }

    return 0;
}

#endif /* ifndef _WIN32 */

static struct {
    Tn5250ErrorType type;
    unsigned long code; // for compat with OpenSSL
} tn5250_error;

void _tn5250_set_error(Tn5250ErrorType type, int code) {
    tn5250_error.type = type;
    tn5250_error.code = code;
}

int tn5250_has_error(void) { return tn5250_error.type != TN5250_ERROR_UNKNOWN; }

void tn5250_clear_error(void) { _tn5250_set_error(0, 0); }

const char* tn5250_strerror(void) {
    if (tn5250_error.type == TN5250_ERROR_ERRNO) {
        return strerror(tn5250_error.code);
    }
    else if (tn5250_error.type == TN5250_ERROR_GAI) {
        return gai_strerror(tn5250_error.code);
    }
#ifdef HAVE_LIBSSL
    else if (tn5250_error.type == TN5250_ERROR_SSL) {
        return ERR_error_string(tn5250_error.code, NULL);
    }
#endif
    else if (tn5250_error.type == TN5250_ERROR_INTERNAL) {
        switch (tn5250_error.code) {
        case TN5250_INTERNALERROR_INVALIDADDRESS:
            return "Invalid address";
        case TN5250_INTERNALERROR_INVALIDCERT:
            return "Certificate verification failure";
        }
    }
    return NULL;
}

/****f* lib5250/tn5250_char_map_to_remote
 * NAME
 *    tn5250_char_map_to_remote
 * SYNOPSIS
 *    ret = tn5250_char_map_to_remote (map,ascii);
 * INPUTS
 *    Tn5250Char           ascii      - The local character to translate.
 * DESCRIPTION
 *    Translate the specified character from local to remote.
 *****/
Tn5250Char tn5250_char_map_to_remote(Tn5250CharMap* map, wchar_t ascii) {
   wchar_t in[2], *inp = in;
   char out[7], *outp = out;
   in[0] = ascii;
   in[1] = '\0';
   out[0] = '\0';
   size_t inleft = 1, outleft = 6;
   iconv(map->to_remote_conv, (char**)&inp, &inleft, (char**)&outp, &outleft);
   return out[0];
}

/****f* lib5250/tn5250_char_map_to_local
 * NAME
 *    tn5250_char_map_to_local
 * SYNOPSIS
 *    local = tn5250_char_map_to_local (map, ebcdic);
 * INPUTS
 *    Tn5250Char           ebcdic     - The remote character to translate.
 * DESCRIPTION
 *    Translate the specified character from remote character to local.
 *****/
wchar_t tn5250_char_map_to_local(Tn5250CharMap* map, Tn5250Char ebcdic) {
    switch (ebcdic) {
    case 0x1C:
        return L'*'; /* This should be an overstriken asterisk (DUP) */
    case 0:
        return L' ';
    default:
        {
            char in[7], *inp = in;
            wchar_t out[2], *outp = out;
            in[0] = ebcdic;
            in[1] = '\0';
            out[0] = '\0';
            size_t inleft = 1, outleft = 6;
            iconv(map->to_local_conv, (char**)&inp, &inleft, (char**)&outp, &outleft);
            return out[0];
        }
    }
}

int tn5250_encoding_name_works(const char *encoding)
{
    iconv_t test_iconv = iconv_open(encoding, "UTF-8");
    if (test_iconv == (iconv_t)-1) {
        return 1;
    }
    return iconv_close(test_iconv) == 0;
}

char *tn5250_encoding_name(const char *map)
{
    /* The iconv names for encodings are pretty inconsistent, unfortunately.
     * We can try some fallbacks for names if the common names in most
     * implementations don't work. On AIX, in theory, we could use ccsidtocs.
     */
#define ENCODING_MAPPING(input, output) \
    if(strcmp(map, input) == 0 && tn5250_encoding_name_works(output)) \
        return strdup(output)
    /* US */
    ENCODING_MAPPING("37", "EBCDIC-CP-US");
    /* Netherlands */
    ENCODING_MAPPING("256", "EBCDIC-CP-NL");
    /* Germany, Austria */
    ENCODING_MAPPING("273", "EBCDIC-AT-DE");
    /* Norway, Denmark */
    ENCODING_MAPPING("277", "EBCDIC-CP-DK");
    /* Sweden, Finland */
    ENCODING_MAPPING("277", "EBCDIC-CP-FI");
    /* Italy */
    ENCODING_MAPPING("280", "EBCDIC-CP-IT");
    /* Japanese (Kana) - japanese encodings are a mess... */
    ENCODING_MAPPING("290", "EBCDIC-JP-KANA");
    /* Spain, Latin America */
    ENCODING_MAPPING("284", "EBCDIC-CP-ES");
    /* UK */
    ENCODING_MAPPING("285", "EBCDIC-CP-GB");
    /* France */
    ENCODING_MAPPING("297", "EBCDIC-CP-FR");
    /* Arabic */
    ENCODING_MAPPING("420", "EBCDIC-CP-AR1");
    /* Hebrew */
    ENCODING_MAPPING("420", "EBCDIC-CP-HE");
    /* Belgium, Canada, Switzerland, Latin-1 */
    ENCODING_MAPPING("500", "EBCDIC-CP-BE");
    /* Latin-2 Multilingual (Yugoslav?) */
    ENCODING_MAPPING("870", "EBCDIC-CP-YU");
    /* Iceland */
    ENCODING_MAPPING("871", "EBCDIC-CP-IS");
    /* Greece */
    ENCODING_MAPPING("875", "EBCDIC-GREEK");
    /* Cyrillic Multilingual */
    ENCODING_MAPPING("880", "EBCDIC-CYRILLIC");
    /* Turkish, Latin-3 Multilingual */
    ENCODING_MAPPING("905", "EBCDIC-CP-TR");
    /* XXX: Fallback names for IBM/IBM- prefixes */
    return NULL;
}

/****f* lib5250/tn5250_char_map_new
 * NAME
 *    tn5250_char_map_new
 * SYNOPSIS
 *    cmap = tn5250_char_map_new ("37");
 * INPUTS
 *    const char *         map        - Name of the character translation map.
 * DESCRIPTION
 *    Create a new translation map.
 * NOTES
 *    Translation maps are currently statically allocated, although you should
 *    call tn5250_char_map_destroy (a no-op) for future compatibility.
 *****/
Tn5250CharMap* tn5250_char_map_new(const char* map) {
    TN5250_LOG(("tn5250_char_map_new: map = \"%s\"\n", map));

    char *encoding_name = tn5250_encoding_name(map);
    TN5250_LOG(("using iconv encoding name \"%s\"\n", encoding_name));
    if (encoding_name == NULL) {
        return NULL;
    }
    iconv_t to_remote_conv = iconv_open(encoding_name, "WCHAR_T");
    if (to_remote_conv == (iconv_t)-1) {
        free(encoding_name);
        return NULL;
    }
    iconv_t to_local_conv = iconv_open("WCHAR_T", encoding_name);
    if (to_local_conv == (iconv_t)-1) {
        free(encoding_name);
        iconv_close(to_remote_conv);
        return NULL;
    }

    Tn5250CharMap* t = calloc(1, sizeof(Tn5250CharMap));
    t->name = encoding_name;
    t->to_remote_conv = to_remote_conv;
    t->to_local_conv = to_local_conv;

    return t;
}

/****f* lib5250/tn5250_char_map_destroy
 * NAME
 *    tn5250_char_map_destroy
 * SYNOPSIS
 *    tn5250_char_map_destroy (map);
 * INPUTS
 *    Tn5250CharMap *         map     - The character map to destroy.
 * DESCRIPTION
 *    Frees the character map's resources.
 *****/
void tn5250_char_map_destroy(Tn5250CharMap* map) {
    free(map->name);
    iconv_close(map->to_remote_conv);
    iconv_close(map->to_local_conv);
    free(map);
}

/****f* lib5250/tn5250_char_map_printable_p
 * NAME
 *    tn5250_char_map_printable_p
 * SYNOPSIS
 *    if (tn5250_char_map_printable_p (map,ec))
 *	 ;
 * INPUTS
 *    Tn5250CharMap *      map        - the character map to use.
 *    Tn5250Char           ec         - the character to test.
 * DESCRIPTION
 *    Determines whether the specified character is printable, which means
 *    either it is a displayable EBCDIC character, an ideographic control
 *    character, a NUL, or a few other odds and ends.
 * SOURCE
 */
int tn5250_char_map_printable_p(Tn5250CharMap* map, Tn5250Char data) {
    switch (data) {
        /*
           Ideographic Shift-In and Shift-Out.
           case 0x0e:
           case 0x0f:
        */
    default:
        break;
    }
    return 1;
}
/*******/

/****f* lib5250/tn5250_char_map_attribute_p
 * NAME
 *    tn5250_char_map_attribute_p
 * SYNOPSIS
 *    ret = tn5250_char_map_attribute_p (map,ec);
 * INPUTS
 *    Tn5250CharMap *      map        - the translation map to use.
 *    Tn5250Char           ec         - the character to test.
 * DESCRIPTION
 *    Determines whether the character is a 5250 attribute.
 *****/
int tn5250_char_map_attribute_p(Tn5250CharMap* map, Tn5250Char data) {
    return ((data & 0xE0) == 0x20);
}

#ifndef NDEBUG
FILE* tn5250_logfile = NULL;

/****f* lib5250/tn5250_log_open
 * NAME
 *    tn5250_log_open
 * SYNOPSIS
 *    tn5250_log_open (fname);
 * INPUTS
 *    const char *         fname      - Filename of tracefile.
 * DESCRIPTION
 *    Opens the debug tracefile for this session.
 *****/
void tn5250_log_open(const char* fname) {
    if (tn5250_logfile != NULL) {
        fclose(tn5250_logfile);
    }
    tn5250_logfile = fopen(fname, "w");
    if (tn5250_logfile == NULL) {
        perror(fname);
        exit(1);
    }
    /* FIXME: Write $TERM, version, and uname -a to the file. */
#ifndef _WIN32
    /* Set file mode to 0600 since it may contain passwords. */
    fchmod(fileno(tn5250_logfile), 0600);
#endif
    setbuf(tn5250_logfile, NULL);
}

/****f* lib5250/tn5250_log_close
 * NAME
 *    tn5250_log_close
 * SYNOPSIS
 *    tn5250_log_close ();
 * INPUTS
 *    None
 * DESCRIPTION
 *    Close the current tracefile if one is open.
 *****/
void tn5250_log_close() {
    if (tn5250_logfile != NULL) {
        fclose(tn5250_logfile);
        tn5250_logfile = NULL;
    }
}

/****f* lib5250/tn5250_log_printf
 * NAME
 *    tn5250_log_printf
 * SYNOPSIS
 *    tn5250_log_printf (fmt, );
 * INPUTS
 *    const char *         fmt        -
 * DESCRIPTION
 *    This is an internal function called by the TN5250_LOG() macro.  Use
 *    the macro instead, since it can be conditionally compiled.
 *****/
void tn5250_log_printf(const char* fmt, ...) {
    va_list vl;
    if (tn5250_logfile != NULL) {
        va_start(vl, fmt);
        vfprintf(tn5250_logfile, fmt, vl);
        va_end(vl);
    }
}

/****f* lib5250/tn5250_log_assert
 * NAME
 *    tn5250_log_assert
 * SYNOPSIS
 *    tn5250_log_assert (val, expr, file, line);
 * INPUTS
 *    int                  val        -
 *    char const *         expr       -
 *    char const *         file       -
 *    int                  line       -
 * DESCRIPTION
 *    This is an internal function called by the TN5250_ASSERT() macro.  Use
 *    the macro instead, since it can be conditionally compiled.
 *****/
void tn5250_log_assert(int val, char const* expr, char const* file, int line) {
    if (!val) {
        tn5250_log_printf("\nAssertion %s failed at %s, line %d.\n", expr, file,
                          line);
        fprintf(stderr, "\nAssertion %s failed at %s, line %d.\n", expr, file,
                line);
        abort();
    }
}
#endif /* NDEBUG */

/****f* lib5250/tn5250_parse_color
 * NAME
 *    tn5250_parse_color
 * SYNOPSIS
 *    tn5250_parse_color (config, "green", &red, &green, &blue);
 * INPUTS
 *    Tn5250Config *       config     -
 *    const char   *       colorname  -
 *    int          *       red        -
 *    int          *       green      -
 *    int          *       blue       -
 * DESCRIPTION
 *    This loads a color from the TN5250 config object, and then
 *    parses it into it's red, green, blue components.
 *****/
int tn5250_parse_color(Tn5250Config* config, const char* colorname, int* red,
                       int* green, int* blue) {

    const char* p;
    char colorspec[16];
    int r, g, b;

    if ((p = tn5250_config_get(config, colorname)) == NULL) {
        return -1;
    }

    strncpy(colorspec, p, sizeof(colorspec));
    colorspec[sizeof(colorspec) - 1] = '\0';

    if (*colorspec != '#') {
        if (!strcasecmp(colorspec, "white")) {
            r = 255;
            g = 255;
            b = 255;
        }
        else if (!strcasecmp(colorspec, "yellow")) {
            r = 255;
            g = 255;
            b = 0;
        }
        else if (!strcasecmp(colorspec, "lightmagenta")) {
            r = 255;
            g = 0;
            b = 255;
        }
        else if (!strcasecmp(colorspec, "lightred")) {
            r = 255;
            g = 0;
            b = 0;
        }
        else if (!strcasecmp(colorspec, "lightcyan")) {
            r = 0;
            g = 255;
            b = 255;
        }
        else if (!strcasecmp(colorspec, "lightgreen")) {
            r = 0;
            g = 255;
            b = 0;
        }
        else if (!strcasecmp(colorspec, "lightblue")) {
            r = 0;
            g = 0;
            b = 255;
        }
        else if (!strcasecmp(colorspec, "lightgray")) {
            r = 192;
            g = 192;
            b = 192;
        }
        else if (!strcasecmp(colorspec, "gray")) {
            r = 128;
            g = 128;
            b = 128;
        }
        else if (!strcasecmp(colorspec, "brown")) {
            r = 128;
            g = 128;
            b = 0;
        }
        else if (!strcasecmp(colorspec, "red")) {
            r = 128;
            g = 0;
            b = 0;
        }
        else if (!strcasecmp(colorspec, "cyan")) {
            r = 0;
            g = 128;
            b = 128;
        }
        else if (!strcasecmp(colorspec, "green")) {
            r = 0;
            g = 128;
            b = 0;
        }
        else if (!strcasecmp(colorspec, "blue")) {
            r = 0;
            g = 0;
            b = 128;
        }
        else if (!strcasecmp(colorspec, "black")) {
            r = 0;
            g = 0;
            b = 0;
        }
    }
    else {
        if (strlen(colorspec) != 7) {
            return -1;
        }
        if (sscanf(&colorspec[1], "%02x%02x%02x", &r, &g, &b) != 3) {
            return -1;
        }
    }

    *red = r;
    *green = g;
    *blue = b;
    return 0;
}

/****f* lib5250/tn5250_run_cmd
 * NAME
 *    tn5250_run_cmd
 * SYNOPSIS
 *    tn5250_run_cmd ("notepad", 0);
 * INPUTS
 *    const char   *       cmd        -
 *    int                  wait       -
 * DESCRIPTION
 *    Run a command (submitted to us by the AS/400)
 *****/
#ifndef _WIN32
int tn5250_run_cmd(const char* cmd, int wait) {

    struct sigaction sa;
    int sig;
    int cpid;

    sa.sa_handler = sig_child;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

#ifdef SIGCHLD
    sigaction(SIGCHLD, &sa, NULL);
#else
#ifdef SIGCLD
    sigaction(SIGCLD, &sa, NULL);
#endif
#endif

    switch (cpid = fork()) {
    case -1:
        return -1;

    case 0:
        system(cmd);
        _exit(0);
    }

    if (wait) {
        waitpid(cpid, NULL, 0);
    }

    return 0;
}
#endif

#ifdef _WIN32
int win32_run_process(const char* cmd, int wait);

int tn5250_run_cmd(const char* cmd, int wait) {

    char* tempcmd;
    char* prefix = "cmd.exe /c ";
    int rc;

    rc = win32_run_process(cmd, wait);

    if (rc == -1) {

        tempcmd = malloc(strlen(prefix) + strlen(cmd) + 1);
        strcpy(tempcmd, prefix);
        strcat(tempcmd, cmd);

        rc = win32_run_process(tempcmd, wait);
        free(tempcmd);
    }

    return rc;
}

int win32_run_process(const char* cmd, int wait) {

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(si));
    si.cb = sizeof(si);

    if (!CreateProcess(NULL,        /* no module name */
                       (LPTSTR)cmd, /* command line */
                       NULL,        /* don't inherit process bandle */
                       NULL,        /* don't inherit thread handle */
                       FALSE,       /* no inheritance, by golly! */
                       0,           /* flags */
                       NULL,        /* use parent's environment */
                       NULL,        /* use parent's directory */
                       &si,         /* STARTUPINFO */
                       &pi)) {      /* PROCESS_INFORMATION */
        return -1;
    }

    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

#endif /* ifdef _WIN32 */
