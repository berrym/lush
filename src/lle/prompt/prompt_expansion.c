/**
 * @file prompt_expansion.c
 * @brief Unified Prompt Expansion Engine (Spec 28 Phase 1)
 *
 * Two-pass architecture:
 *   Pass 1: lle_template_evaluate() resolves ${...} segments
 *   Pass 2: Single scan expands \X (bash) and %X (zsh) escapes
 *
 * All dependencies are POSIX — no GNU extensions.
 */

#include "lle/prompt/prompt_expansion.h"
#include "version.h"

#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Internal buffer for intermediate pass-1 output */
#define EXPAND_BUF_SIZE 4096

/* ========================================================================== */
/* Helper: safe append to output buffer                                       */
/* ========================================================================== */

typedef struct {
    char *buf;
    size_t size;
    size_t pos;
} expand_buf_t;

static void buf_init(expand_buf_t *b, char *buf, size_t size) {
    b->buf = buf;
    b->size = size;
    b->pos = 0;
    if (size > 0)
        buf[0] = '\0';
}

static void buf_append_char(expand_buf_t *b, char c) {
    if (b->pos + 1 < b->size) {
        b->buf[b->pos++] = c;
        b->buf[b->pos] = '\0';
    }
}

static void buf_append_str(expand_buf_t *b, const char *s) {
    if (!s)
        return;
    while (*s && b->pos + 1 < b->size) {
        b->buf[b->pos++] = *s++;
    }
    if (b->size > 0)
        b->buf[b->pos < b->size ? b->pos : b->size - 1] = '\0';
}

static void buf_append_int(expand_buf_t *b, int val) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", val);
    buf_append_str(b, tmp);
}

/* ========================================================================== */
/* Helper: get cached system values                                           */
/* ========================================================================== */

static const char *get_username(void) {
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_name : "";
}

static void get_hostname_short(char *buf, size_t size) {
    if (gethostname(buf, size) != 0) {
        buf[0] = '\0';
        return;
    }
    buf[size - 1] = '\0';
    char *dot = strchr(buf, '.');
    if (dot)
        *dot = '\0';
}

static void get_hostname_full(char *buf, size_t size) {
    if (gethostname(buf, size) != 0)
        buf[0] = '\0';
    else
        buf[size - 1] = '\0';
}

static void get_cwd_full(char *buf, size_t size) {
    if (!getcwd(buf, size))
        buf[0] = '\0';
}

/**
 * Get cwd with home directory replaced by ~
 */
static void get_cwd_tilde(char *buf, size_t size) {
    char cwd[PATH_MAX];
    get_cwd_full(cwd, sizeof(cwd));

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        size_t hlen = strlen(pw->pw_dir);
        if (strncmp(cwd, pw->pw_dir, hlen) == 0 &&
            (cwd[hlen] == '/' || cwd[hlen] == '\0')) {
            snprintf(buf, size, "~%s", cwd + hlen);
            return;
        }
    }
    snprintf(buf, size, "%s", cwd);
}

/**
 * Get basename of cwd (with ~ substitution for home)
 */
static void get_cwd_basename(char *buf, size_t size) {
    char tilde[PATH_MAX];
    get_cwd_tilde(tilde, sizeof(tilde));

    /* Home dir itself → ~ */
    if (strcmp(tilde, "~") == 0) {
        snprintf(buf, size, "~");
        return;
    }
    /* Root → / */
    if (strcmp(tilde, "/") == 0) {
        snprintf(buf, size, "/");
        return;
    }
    const char *last = strrchr(tilde, '/');
    snprintf(buf, size, "%s", last ? last + 1 : tilde);
}

static const char *get_tty_name(void) {
    const char *tty = ttyname(STDIN_FILENO);
    if (!tty)
        return "?";
    /* Return just the device part (e.g., "pts/0") */
    const char *dev = strstr(tty, "/dev/");
    return dev ? dev + 5 : tty;
}

/* ========================================================================== */
/* Helper: parse color spec for %F{color} / %K{color}                        */
/* ========================================================================== */

/**
 * Parse a color name/number/hex and emit the ANSI escape.
 * fg=true for foreground (%F), fg=false for background (%K).
 */
static void emit_color(expand_buf_t *b, const char *spec, int color_depth,
                       bool fg) {
    if (color_depth == 0)
        return;

    /* Hex color: #RRGGBB */
    if (spec[0] == '#' && strlen(spec) == 7) {
        unsigned int r, g, bl;
        if (sscanf(spec + 1, "%2x%2x%2x", &r, &g, &bl) == 3) {
            if (color_depth >= 3) {
                char esc[32];
                snprintf(esc, sizeof(esc), "\033[%d;2;%u;%u;%um",
                         fg ? 38 : 48, r, g, bl);
                buf_append_str(b, esc);
            } else if (color_depth >= 2) {
                /* Approximate to 256-color cube */
                int ri = (r > 47) ? (r - 35) / 40 : 0;
                int gi = (g > 47) ? (g - 35) / 40 : 0;
                int bi = (bl > 47) ? (bl - 35) / 40 : 0;
                int idx = 16 + 36 * ri + 6 * gi + bi;
                char esc[24];
                snprintf(esc, sizeof(esc), "\033[%d;5;%dm", fg ? 38 : 48, idx);
                buf_append_str(b, esc);
            }
            /* color_depth==1: skip, no good 8-color approximation */
            return;
        }
    }

    /* Numeric: 0-255 */
    char *end;
    long num = strtol(spec, &end, 10);
    if (*end == '\0' && end != spec && num >= 0 && num <= 255) {
        if (color_depth >= 2) {
            char esc[24];
            snprintf(esc, sizeof(esc), "\033[%d;5;%ldm", fg ? 38 : 48, num);
            buf_append_str(b, esc);
        } else {
            /* Map 256-color index to basic 8 */
            int code = (int)(num % 8);
            char esc[16];
            snprintf(esc, sizeof(esc), "\033[%dm", (fg ? 30 : 40) + code);
            buf_append_str(b, esc);
        }
        return;
    }

    /* Named colors */
    static const struct {
        const char *name;
        int code;
    } named[] = {
        {"black", 0},   {"red", 1},  {"green", 2},  {"yellow", 3},
        {"blue", 4},    {"magenta", 5}, {"cyan", 6}, {"white", 7},
        {"default", 9},
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (strcmp(spec, named[i].name) == 0) {
            char esc[16];
            snprintf(esc, sizeof(esc), "\033[%dm",
                     (fg ? 30 : 40) + named[i].code);
            buf_append_str(b, esc);
            return;
        }
    }
    /* Unknown color: silently ignore */
}

/* ========================================================================== */
/* Pass 2: Expand bash \X and zsh %X escapes                                  */
/* ========================================================================== */

static lle_result_t expand_prompt_escapes(const char *input, char *output,
                                          size_t size,
                                          const lle_prompt_expand_ctx_t *ctx) {
    expand_buf_t b;
    buf_init(&b, output, size);

    const char *p = input;
    while (*p) {
        /* ------------------------------------------------------------ */
        /* Skip ANSI escape sequences (from pass-1 template rendering)  */
        /* ESC (0x1B) followed by [ ... final byte                      */
        /* ------------------------------------------------------------ */
        if (*p == '\033') {
            buf_append_char(&b, *p++);
            if (*p == '[') {
                buf_append_char(&b, *p++);
                /* Copy parameter bytes + final byte */
                while (*p && *p < 0x40)  {
                    buf_append_char(&b, *p++);
                }
                if (*p) {
                    buf_append_char(&b, *p++); /* final byte */
                }
            }
            continue;
        }

        /* ------------------------------------------------------------ */
        /* Bash escapes: \X                                             */
        /* ------------------------------------------------------------ */
        if (*p == '\\' && *(p + 1)) {
            char next = *(p + 1);
            p += 2; /* consume \ and next char */

            switch (next) {
            case 'u': { /* username */
                buf_append_str(&b, get_username());
                break;
            }
            case 'h': { /* short hostname */
                char host[256];
                get_hostname_short(host, sizeof(host));
                buf_append_str(&b, host);
                break;
            }
            case 'H': { /* full hostname */
                char host[256];
                get_hostname_full(host, sizeof(host));
                buf_append_str(&b, host);
                break;
            }
            case 'w': { /* cwd with ~ */
                char cwd[PATH_MAX];
                get_cwd_tilde(cwd, sizeof(cwd));
                buf_append_str(&b, cwd);
                break;
            }
            case 'W': { /* cwd basename */
                char cwd[PATH_MAX];
                get_cwd_basename(cwd, sizeof(cwd));
                buf_append_str(&b, cwd);
                break;
            }
            case 'd': { /* date: "Sat Feb 22" */
                time_t now = time(NULL);
                struct tm tm_buf;
                localtime_r(&now, &tm_buf);
                char date[64];
                strftime(date, sizeof(date), "%a %b %d", &tm_buf);
                buf_append_str(&b, date);
                break;
            }
            case 't': { /* time: HH:MM:SS 24h */
                time_t now = time(NULL);
                struct tm tm_buf;
                localtime_r(&now, &tm_buf);
                char t[16];
                strftime(t, sizeof(t), "%H:%M:%S", &tm_buf);
                buf_append_str(&b, t);
                break;
            }
            case 'T': { /* time: HH:MM:SS 12h */
                time_t now = time(NULL);
                struct tm tm_buf;
                localtime_r(&now, &tm_buf);
                char t[16];
                strftime(t, sizeof(t), "%I:%M:%S", &tm_buf);
                buf_append_str(&b, t);
                break;
            }
            case '@': { /* time: 12h am/pm */
                time_t now = time(NULL);
                struct tm tm_buf;
                localtime_r(&now, &tm_buf);
                char t[16];
                strftime(t, sizeof(t), "%I:%M %p", &tm_buf);
                buf_append_str(&b, t);
                break;
            }
            case 'A': { /* time: HH:MM 24h */
                time_t now = time(NULL);
                struct tm tm_buf;
                localtime_r(&now, &tm_buf);
                char t[16];
                strftime(t, sizeof(t), "%H:%M", &tm_buf);
                buf_append_str(&b, t);
                break;
            }
            case '$': { /* # if root, $ otherwise */
                buf_append_char(&b, getuid() == 0 ? '#' : '$');
                break;
            }
            case 'n': { /* newline */
                buf_append_char(&b, '\n');
                break;
            }
            case 'r': { /* carriage return */
                buf_append_char(&b, '\r');
                break;
            }
            case '\\': { /* literal backslash */
                buf_append_char(&b, '\\');
                break;
            }
            case '[': { /* begin non-printing: stripped */
                break;
            }
            case ']': { /* end non-printing: stripped */
                break;
            }
            case '!': { /* history number */
                buf_append_int(&b, ctx->history_number);
                break;
            }
            case '#': { /* command number */
                buf_append_int(&b, ctx->command_number);
                break;
            }
            case 'j': { /* job count */
                buf_append_int(&b, ctx->job_count);
                break;
            }
            case 'l': { /* tty device basename */
                const char *tty = get_tty_name();
                const char *base = strrchr(tty, '/');
                buf_append_str(&b, base ? base + 1 : tty);
                break;
            }
            case 's': { /* shell name */
                buf_append_str(&b, LUSH_NAME);
                break;
            }
            case 'v': { /* shell version short */
                char ver[16];
                snprintf(ver, sizeof(ver), "%d.%d", LUSH_VERSION_MAJOR,
                         LUSH_VERSION_MINOR);
                buf_append_str(&b, ver);
                break;
            }
            case 'V': { /* shell version full */
                buf_append_str(&b, LUSH_VERSION_STRING);
                break;
            }
            case 'e': { /* ESC character */
                buf_append_char(&b, '\033');
                break;
            }
            case 'a': { /* BEL character */
                buf_append_char(&b, '\a');
                break;
            }
            case '0': { /* octal: \0NNN */
                unsigned int val = 0;
                int digits = 0;
                while (digits < 3 && *p >= '0' && *p <= '7') {
                    val = val * 8 + (unsigned int)(*p - '0');
                    p++;
                    digits++;
                }
                if (val <= 255)
                    buf_append_char(&b, (char)val);
                break;
            }
            case 'x': { /* hex: \xNN */
                unsigned int val = 0;
                int digits = 0;
                while (digits < 2 &&
                       ((*p >= '0' && *p <= '9') ||
                        (*p >= 'a' && *p <= 'f') ||
                        (*p >= 'A' && *p <= 'F'))) {
                    if (*p >= '0' && *p <= '9')
                        val = val * 16 + (unsigned int)(*p - '0');
                    else if (*p >= 'a' && *p <= 'f')
                        val = val * 16 + (unsigned int)(*p - 'a' + 10);
                    else
                        val = val * 16 + (unsigned int)(*p - 'A' + 10);
                    p++;
                    digits++;
                }
                if (val <= 255)
                    buf_append_char(&b, (char)val);
                break;
            }
            default:
                /* Unknown bash escape: pass through literally */
                buf_append_char(&b, '\\');
                buf_append_char(&b, next);
                break;
            }
            continue;
        }

        /* ------------------------------------------------------------ */
        /* Zsh escapes: %X                                              */
        /* ------------------------------------------------------------ */
        if (*p == '%' && *(p + 1)) {
            char next = *(p + 1);
            p += 2;

            switch (next) {
            case 'n': { /* username */
                buf_append_str(&b, get_username());
                break;
            }
            case 'm': { /* short hostname */
                char host[256];
                get_hostname_short(host, sizeof(host));
                buf_append_str(&b, host);
                break;
            }
            case 'M': { /* full hostname */
                char host[256];
                get_hostname_full(host, sizeof(host));
                buf_append_str(&b, host);
                break;
            }
            case 'd': /* fall through */
            case '/': { /* full cwd */
                char cwd[PATH_MAX];
                get_cwd_full(cwd, sizeof(cwd));
                buf_append_str(&b, cwd);
                break;
            }
            case '~': { /* cwd with ~ */
                char cwd[PATH_MAX];
                get_cwd_tilde(cwd, sizeof(cwd));
                buf_append_str(&b, cwd);
                break;
            }
            case 'c': /* fall through */
            case '.': { /* cwd tail component */
                char cwd[PATH_MAX];
                get_cwd_basename(cwd, sizeof(cwd));
                buf_append_str(&b, cwd);
                break;
            }
            case '#': { /* # if root, % otherwise (zsh convention) */
                buf_append_char(&b, getuid() == 0 ? '#' : '%');
                break;
            }
            case '%': { /* literal % */
                buf_append_char(&b, '%');
                break;
            }
            case 'T': { /* time HH:MM 24h */
                time_t now = time(NULL);
                struct tm tm_buf;
                localtime_r(&now, &tm_buf);
                char t[16];
                strftime(t, sizeof(t), "%H:%M", &tm_buf);
                buf_append_str(&b, t);
                break;
            }
            case 't': /* fall through */
            case '@': { /* time 12h am/pm */
                time_t now = time(NULL);
                struct tm tm_buf;
                localtime_r(&now, &tm_buf);
                char t[16];
                strftime(t, sizeof(t), "%l:%M %p", &tm_buf);
                buf_append_str(&b, t);
                break;
            }
            case '*': { /* time HH:MM:SS 24h */
                time_t now = time(NULL);
                struct tm tm_buf;
                localtime_r(&now, &tm_buf);
                char t[16];
                strftime(t, sizeof(t), "%H:%M:%S", &tm_buf);
                buf_append_str(&b, t);
                break;
            }
            case 'j': { /* job count */
                buf_append_int(&b, ctx->job_count);
                break;
            }
            case 'l': { /* tty device name */
                buf_append_str(&b, get_tty_name());
                break;
            }
            case '?': { /* last exit status */
                buf_append_int(&b, ctx->last_exit_status);
                break;
            }
            case 'D': { /* date with format: %D{fmt} */
                if (*p == '{') {
                    p++; /* skip { */
                    const char *start = p;
                    while (*p && *p != '}')
                        p++;
                    size_t fmtlen = (size_t)(p - start);
                    if (*p == '}')
                        p++; /* skip } */
                    char fmt[128];
                    if (fmtlen >= sizeof(fmt))
                        fmtlen = sizeof(fmt) - 1;
                    memcpy(fmt, start, fmtlen);
                    fmt[fmtlen] = '\0';
                    time_t now = time(NULL);
                    struct tm tm_buf;
                    localtime_r(&now, &tm_buf);
                    char date[256];
                    strftime(date, sizeof(date), fmt, &tm_buf);
                    buf_append_str(&b, date);
                } else {
                    /* %D without {}: output date in default format */
                    time_t now = time(NULL);
                    struct tm tm_buf;
                    localtime_r(&now, &tm_buf);
                    char date[32];
                    strftime(date, sizeof(date), "%y-%m-%d", &tm_buf);
                    buf_append_str(&b, date);
                }
                break;
            }
            case 'B': { /* bold on */
                buf_append_str(&b, "\033[1m");
                break;
            }
            case 'b': { /* bold off */
                buf_append_str(&b, "\033[22m");
                break;
            }
            case 'U': { /* underline on */
                buf_append_str(&b, "\033[4m");
                break;
            }
            case 'u': { /* underline off */
                buf_append_str(&b, "\033[24m");
                break;
            }
            case 'S': { /* standout (reverse) on */
                buf_append_str(&b, "\033[7m");
                break;
            }
            case 's': { /* standout off */
                buf_append_str(&b, "\033[27m");
                break;
            }
            case 'F': { /* foreground color: %F{spec} */
                if (*p == '{') {
                    p++;
                    const char *start = p;
                    while (*p && *p != '}')
                        p++;
                    size_t len = (size_t)(p - start);
                    if (*p == '}')
                        p++;
                    char spec[64];
                    if (len >= sizeof(spec))
                        len = sizeof(spec) - 1;
                    memcpy(spec, start, len);
                    spec[len] = '\0';
                    emit_color(&b, spec, ctx->color_depth, true);
                }
                break;
            }
            case 'f': { /* reset foreground */
                buf_append_str(&b, "\033[39m");
                break;
            }
            case 'K': { /* background color: %K{spec} */
                if (*p == '{') {
                    p++;
                    const char *start = p;
                    while (*p && *p != '}')
                        p++;
                    size_t len = (size_t)(p - start);
                    if (*p == '}')
                        p++;
                    char spec[64];
                    if (len >= sizeof(spec))
                        len = sizeof(spec) - 1;
                    memcpy(spec, start, len);
                    spec[len] = '\0';
                    emit_color(&b, spec, ctx->color_depth, false);
                }
                break;
            }
            case 'k': { /* reset background */
                buf_append_str(&b, "\033[49m");
                break;
            }
            default:
                /* Unknown zsh escape: pass through literally */
                buf_append_char(&b, '%');
                buf_append_char(&b, next);
                break;
            }
            continue;
        }

        /* ------------------------------------------------------------ */
        /* Regular character: copy through                              */
        /* ------------------------------------------------------------ */
        buf_append_char(&b, *p++);
    }

    return LLE_SUCCESS;
}

/* ========================================================================== */
/* Public API: lle_prompt_expand                                              */
/* ========================================================================== */

lle_result_t lle_prompt_expand(const char *format, char *output,
                               size_t output_size,
                               const lle_prompt_expand_ctx_t *ctx) {
    if (!format || !output || output_size == 0 || !ctx)
        return LLE_ERROR_NULL_POINTER;

    output[0] = '\0';

    /*
     * Pass 1: Resolve LLE template segments (${...})
     *
     * If template_ctx is provided, run the template engine first.
     * The template engine handles ${segment}, ${?cond:t:f}, ${color:text}
     * and its own \n, \$, \\ escapes. It leaves bash \X and zsh %X untouched
     * because it only recognizes \n, \\, and \$ as escapes.
     *
     * NOTE: The template engine treats \n as newline and \\ as backslash.
     * Bash prompt escapes like \u, \h etc. will pass through the template
     * engine as literal characters since it doesn't recognize them.
     */
    const char *pass2_input = format;
    char intermediate[EXPAND_BUF_SIZE];

    if (ctx->template_ctx) {
        lle_result_t r = lle_template_evaluate(format, ctx->template_ctx,
                                               intermediate,
                                               sizeof(intermediate));
        if (r != LLE_SUCCESS)
            return r;
        pass2_input = intermediate;
    }

    /* Pass 2: Expand bash \X and zsh %X escapes */
    return expand_prompt_escapes(pass2_input, output, output_size, ctx);
}
