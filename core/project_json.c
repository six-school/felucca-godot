/* core/project_json.h. A JSON reader just big enough for one known object: members it does not read are skipped
 * (checked as JSON all the same), a repeated member's last value wins (as JSON.parse); strict where the file's meaning
 * is: the types of the members read, plain integers, base64. */
#include "project_json.h"
#include <string.h>

#define MAX_DEPTH 64                    /* nesting of skipped values (a hostile file must not overflow the stack) */
static const uint32_t SIZES[] = {3840u, 3648u, 3584u, 3388u};   /* FUN10 FUN9 FUN8 FUN7 (editor_backup.c) */

uint32_t fel_crc32(const uint8_t *b, size_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8; k++)
            c = c >> 1 ^ (c & 1u ? 0xEDB88320u : 0u);
    }
    return ~c;
}

const char *fel_project_json_error(int code)
{
    static const char *const MSG[] = {
        "ok", "not JSON", "not a Felucca project (\"format\")", "a project file version other than 1",
        "\"object\" missing or not {size, crc, data}", "an object size the firmware does not read",
        "\"data\" is not base64", "\"data\" does not hold \"size\" bytes", "CRC-32 mismatch: the file is damaged",
    };
    return code >= 0 && code < (int)(sizeof MSG / sizeof MSG[0]) ? MSG[code] : "?";
}

typedef struct { const char *p, *end; int depth; } cur_t;

static void ws(cur_t *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r'))
        c->p++;
}
static int peek(cur_t *c)
{
    ws(c);
    return c->p < c->end ? (unsigned char)*c->p : -1;
}
static int eat(cur_t *c, char ch)
{
    if (peek(c) != (unsigned char)ch)
        return 0;
    c->p++;
    return 1;
}

static int hexval(char h)
{
    return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10
         : -1;
}

/* a string's characters, escapes decoded, each to sink(ctx, code unit): the sink's code stops it (not FEL_PJ_OK) */
typedef int (*sink_t)(void *ctx, uint32_t cu);
static int string(cur_t *c, sink_t sink, void *ctx)
{
    if (!eat(c, '"'))
        return FEL_PJ_SYNTAX;
    while (c->p < c->end) {
        unsigned char ch = (unsigned char)*c->p++;
        uint32_t cu = ch, k;
        int e;
        if (ch == '"')
            return FEL_PJ_OK;
        if (ch < 0x20u)                                 /* control characters come escaped */
            return FEL_PJ_SYNTAX;
        if (ch == '\\') {
            if (c->p >= c->end)
                return FEL_PJ_SYNTAX;
            switch (*c->p++) {
            case '"': cu = '"'; break;
            case '\\': cu = '\\'; break;
            case '/': cu = '/'; break;
            case 'b': cu = 8; break;
            case 'f': cu = 12; break;
            case 'n': cu = 10; break;
            case 'r': cu = 13; break;
            case 't': cu = 9; break;
            case 'u':                                   /* (a UTF-16 code unit: what the members read take is ASCII) */
                if (c->end - c->p < 4)
                    return FEL_PJ_SYNTAX;
                for (cu = 0, k = 0; k < 4; k++) {
                    int h = hexval(*c->p++);
                    if (h < 0)
                        return FEL_PJ_SYNTAX;
                    cu = cu << 4 | (uint32_t)h;
                }
                break;
            default:
                return FEL_PJ_SYNTAX;
            }
        }
        if (sink && (e = sink(ctx, cu)) != FEL_PJ_OK)
            return e;
    }
    return FEL_PJ_SYNTAX;
}

/* a number; *integer: written as a plain integer (what the web app writes) of at most 15 digits, *v its value */
static int number(cur_t *c, int64_t *v, int *integer)
{
    int neg = 0, digits = 0;
    int64_t x = 0;
    ws(c);
    *integer = 1;
    if (c->p < c->end && *c->p == '-') {
        neg = 1;
        c->p++;
    }
    if (c->p < c->end && *c->p == '0') {                /* (no leading zeros) */
        c->p++;
        digits = 1;
    } else
        for (; c->p < c->end && *c->p >= '0' && *c->p <= '9'; c->p++, digits++)
            if (digits < 15)
                x = x * 10 + (*c->p - '0');
            else
                *integer = 0;
    if (!digits)
        return FEL_PJ_SYNTAX;
    if (c->p < c->end && *c->p == '.') {
        *integer = digits = 0;
        for (c->p++; c->p < c->end && *c->p >= '0' && *c->p <= '9'; c->p++)
            digits++;
        if (!digits)
            return FEL_PJ_SYNTAX;
    }
    if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
        *integer = digits = 0;
        c->p++;
        if (c->p < c->end && (*c->p == '+' || *c->p == '-'))
            c->p++;
        for (; c->p < c->end && *c->p >= '0' && *c->p <= '9'; c->p++)
            digits++;
        if (!digits)
            return FEL_PJ_SYNTAX;
    }
    *v = neg ? -x : x;
    return FEL_PJ_OK;
}

static int word(cur_t *c, const char *w)
{
    size_t k = strlen(w);
    if ((size_t)(c->end - c->p) < k || memcmp(c->p, w, k))
        return FEL_PJ_SYNTAX;
    c->p += k;
    return FEL_PJ_OK;
}

/* an object's members: each key (cut to fit k: "" when too long, so it matches no name) to member(), which reads
 * the value */
typedef int (*member_t)(cur_t *c, const char *key, void *ctx);
static int skip(cur_t *c);
static int object(cur_t *c, member_t member, void *ctx);

typedef struct { char *b; size_t n, cap; int cut; } text_t;
static int text_sink(void *ctx, uint32_t cu)
{
    text_t *t = ctx;
    if (t->n + 1u >= t->cap) {
        t->cut = 1;
        return FEL_PJ_OK;
    }
    t->b[t->n++] = cu && cu < 0x80u ? (char)cu : '?';     /* (a NUL must not end a key early: "format\u0000") */
    t->b[t->n] = 0;
    return FEL_PJ_OK;
}
static int text(cur_t *c, char *b, size_t cap, int *cut)
{
    text_t t = {b, 0, cap, 0};
    int e;
    b[0] = 0;
    e = string(c, text_sink, &t);
    *cut = t.cut;
    return e;
}

static int skip_member(cur_t *c, const char *key, void *ctx)
{
    (void)key;
    (void)ctx;
    return skip(c);
}
static int skip(cur_t *c)                               /* any value */
{
    int e, plain, ch = peek(c);
    int64_t v;
    if (ch == '{' || ch == '[') {
        if (++c->depth > MAX_DEPTH)
            return FEL_PJ_SYNTAX;
        if (ch == '{')
            e = object(c, skip_member, 0);
        else {
            c->p++;
            e = FEL_PJ_OK;
            if (!eat(c, ']'))
                for (;;) {
                    if ((e = skip(c)) != FEL_PJ_OK)
                        break;
                    if (eat(c, ']'))
                        break;
                    if (!eat(c, ',')) {
                        e = FEL_PJ_SYNTAX;
                        break;
                    }
                }
        }
        c->depth--;
        return e;
    }
    if (ch == '"')
        return string(c, 0, 0);
    if (ch == 't')
        return word(c, "true");
    if (ch == 'f')
        return word(c, "false");
    if (ch == 'n')
        return word(c, "null");
    return number(c, &v, &plain);
}
static int object(cur_t *c, member_t member, void *ctx)
{
    char k[16];
    int e, cut;
    if (!eat(c, '{'))
        return FEL_PJ_SYNTAX;
    if (eat(c, '}'))
        return FEL_PJ_OK;
    for (;;) {
        if ((e = text(c, k, sizeof k, &cut)) != FEL_PJ_OK)
            return e;
        if (cut)
            k[0] = 0;
        if (!eat(c, ':'))
            return FEL_PJ_SYNTAX;
        if ((e = member(c, k, ctx)) != FEL_PJ_OK)
            return e;
        if (eat(c, '}'))
            return FEL_PJ_OK;
        if (!eat(c, ','))
            return FEL_PJ_SYNTAX;
    }
}

/* ---------------------------------------------------------------- the project file --- */
typedef struct {
    fel_project_file_t *out;
    int format, version, object, size, crc, data;       /* -1 missing; 0 wrong; 1 as it must be */
    int64_t size_v, crc_v;
    int b64;                                            /* FEL_PJ_OK, or FEL_PJ_BASE64 */
    uint32_t n;                                         /* bytes "data" decodes to (counted past the buffer) */
} file_t;

typedef struct { file_t *f; uint32_t acc, bits, chars, pad; } b64_t;
static int b64_sink(void *ctx, uint32_t cu)
{
    b64_t *s = ctx;
    int v = cu >= 'A' && cu <= 'Z' ? (int)(cu - 'A') : cu >= 'a' && cu <= 'z' ? (int)(cu - 'a') + 26
          : cu >= '0' && cu <= '9' ? (int)(cu - '0') + 52 : cu == '+' ? 62 : cu == '/' ? 63 : -1;
    s->chars++;
    if (cu == '=')
        return ++s->pad > 2u ? FEL_PJ_BASE64 : FEL_PJ_OK;
    if (v < 0 || s->pad)                                /* (nothing after the padding) */
        return FEL_PJ_BASE64;
    s->acc = s->acc << 6 | (uint32_t)v;
    s->bits += 6;
    if (s->bits >= 8u) {
        s->bits -= 8;
        if (s->f->n < FEL_PROJECT_SIZE)
            s->f->out->data[s->f->n] = (uint8_t)(s->acc >> s->bits);
        s->f->n++;
    }
    return FEL_PJ_OK;
}
/* -> FEL_PJ_OK when its type is the one expected (*ok 1), FEL_PJ_OK with *ok 0 when not (skipped), else syntax */
static int integer(cur_t *c, int64_t *v, int *ok)
{
    int e, ch = peek(c), plain;
    if (ch != '-' && !(ch >= '0' && ch <= '9')) {
        *ok = 0;
        return skip(c);
    }
    e = number(c, v, &plain);
    *ok = plain;
    return e;
}

static int object_member(cur_t *c, const char *key, void *ctx)
{
    file_t *f = ctx;
    int e, ok;
    if (!strcmp(key, "size")) {
        e = integer(c, &f->size_v, &ok);
        f->size = ok;
    } else if (!strcmp(key, "crc")) {
        e = integer(c, &f->crc_v, &ok);
        f->crc = ok && f->crc_v >= 0 && f->crc_v <= 0xFFFFFFFFll;
    } else if (!strcmp(key, "data")) {
        b64_t s = {f, 0, 0, 0, 0};
        if (peek(c) != '"') {
            f->data = 0;
            return skip(c);
        }
        f->n = 0;
        e = string(c, b64_sink, &s);
        if (e == FEL_PJ_BASE64) {                       /* (the rest of the string, as JSON) */
            f->b64 = FEL_PJ_BASE64;
            f->data = 1;
            while (c->p < c->end && *c->p != '"')
                c->p += *c->p == '\\' && c->p + 1 < c->end ? 2 : 1;
            return eat(c, '"') ? FEL_PJ_OK : FEL_PJ_SYNTAX;
        }
        f->data = 1;
        f->b64 = s.chars % 4u || (s.pad == 1u && s.bits != 2u) || (s.pad == 2u && s.bits != 4u) ? FEL_PJ_BASE64
                                                                                                 : FEL_PJ_OK;
    } else
        e = skip(c);
    return e;
}

static int file_member(cur_t *c, const char *key, void *ctx)
{
    file_t *f = ctx;
    char b[32];
    int e, ok, cut;
    int64_t v;
    if (!strcmp(key, "format")) {
        if (peek(c) != '"') {
            f->format = 0;
            return skip(c);
        }
        e = text(c, b, sizeof b, &cut);
        f->format = !cut && !strcmp(b, "felucca-project");
    } else if (!strcmp(key, "version")) {
        e = integer(c, &v, &ok);
        f->version = ok && v == 1;
    } else if (!strcmp(key, "firmware")) {
        if (peek(c) != '"')
            return skip(c);
        e = text(c, f->out->firmware, sizeof f->out->firmware, &cut);
    } else if (!strcmp(key, "slot")) {
        e = integer(c, &v, &ok);
        if (ok && v >= INT32_MIN && v <= INT32_MAX)
            f->out->slot = (int32_t)v;
    } else if (!strcmp(key, "object")) {
        if (peek(c) != '{') {
            f->object = 0;
            return skip(c);
        }
        f->size = f->crc = f->data = -1;                /* (a repeated "object": its own members only) */
        f->object = 1;
        e = object(c, object_member, f);
    } else
        e = skip(c);
    return e;
}

int fel_project_json(const char *json, size_t n, fel_project_file_t *out)
{
    cur_t c = {json, json + n, 0};
    file_t f;
    int e;
    uint32_t k;
    memset(out, 0, sizeof *out);
    out->slot = -1;
    memset(&f, 0, sizeof f);
    f.out = out;
    f.format = f.version = f.object = f.size = f.crc = f.data = -1;
    if (peek(&c) != '{')
        return FEL_PJ_SYNTAX;
    if ((e = object(&c, file_member, &f)) != FEL_PJ_OK)
        return e;
    if (peek(&c) != -1)                                  /* (one value, then the end) */
        return FEL_PJ_SYNTAX;
    if (f.format != 1)
        return FEL_PJ_FORMAT;
    if (f.version != 1)
        return FEL_PJ_VERSION;
    if (f.object != 1 || f.size < 0 || f.crc != 1 || f.data != 1)
        return FEL_PJ_OBJECT;
    for (k = 0; k < sizeof SIZES / sizeof SIZES[0] && !(f.size && f.size_v == SIZES[k]); k++)
        ;
    if (k == sizeof SIZES / sizeof SIZES[0])
        return FEL_PJ_SIZE;
    if (f.b64 != FEL_PJ_OK)
        return FEL_PJ_BASE64;
    if (f.n != (uint32_t)f.size_v)
        return FEL_PJ_LENGTH;
    out->size = f.n;
    out->crc = (uint32_t)f.crc_v;
    if (fel_crc32(out->data, out->size) != out->crc)
        return FEL_PJ_CRC;
    return FEL_PJ_OK;
}
