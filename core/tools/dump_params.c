/* The firmware's own parameter tables as JSON (docs/DESIGN.md §8, D10): what extension/codegen/gen_api.py makes the
 * typed API of. White-box (board.c compiled in): every track parameter by id (TP, as the editor protocol's
 * param_desc_of reads it), every global with whether the core takes a write of it, the pages (their ids in order),
 * each engine's eight (edit[]).
 *   dump_params > params.json */
#include "board.c"
#include <stdio.h>

static void jstr(const char *s)
{
    putchar('"');
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

static void desc(uint32_t id, const param_desc_t *d)
{
    int v;
    printf("{\"id\":%u,\"label\":", id);
    jstr(d->label);
    printf(",\"fmt\":%d,\"enum\":%s,\"min\":%d,\"max\":%d,\"def\":%d", d->fmt, d->fmt == F_ENUM && d->names ? "true" : "false",
           d->min, d->max, d->def);
    if (d->fmt == F_ENUM && d->names) {
        printf(",\"names\":[");
        for (v = d->min; v <= d->max; v++) {
            if (v > d->min)
                putchar(',');
            jstr(d->names[v]);
        }
        putchar(']');
    }
    putchar('}');
}

int main(void)
{
    uint32_t i, k, e;
    printf("{\"version\":");
    jstr(FELUCCA_VERSION);
    printf(",\"tracks\":%u,\"e0\":%u,\"track\":[", NTRK, P_E0);
    for (i = 0; i < P_COUNT; i++) {
        if (i)
            putchar(',');
        desc(i, &TP[i]);
    }
    printf("],\"global\":[");
    for (i = 0; i < G_COUNT; i++) {
        if (i)
            putchar(',');
        desc(i, &GP[i]);
        printf("\n");
    }
    printf("],\"global_writable\":[");
    for (i = 0, k = 0; i < G_COUNT; i++)
        if (fel_global_writable((int)i))
            printf("%s%u", k++ ? "," : "", i);
    printf("],\"pages\":[");
    for (i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++) {
        printf("%s{\"page\":", i ? "," : "");
        jstr(PAGES[i].title);
        printf(",\"scope\":\"%s\",\"ids\":[", PAGES[i].scope == SC_GLOBAL ? "global" : PAGES[i].scope == SC_ENGINE ? "engine" : "track");
        for (k = 0, e = 0; k < 4; k++)
            if (PAGES[i].id[k] != 0xFF)
                printf("%s%u", e++ ? "," : "", PAGES[i].id[k]);
        printf("]}\n");
    }
    printf("],\"engines\":[");
    for (e = 0; e < NENGINES; e++) {
        printf("%s{\"index\":%u,\"name\":", e ? "," : "", e);
        jstr(ENGINES[e]->name);
        printf(",\"params\":[");
        for (k = 0; k < 8; k++) {
            if (k)
                putchar(',');
            desc(P_E0 + k, &ENGINES[e]->edit[k]);
        }
        printf("]}\n");
    }
    printf("]}\n");
    return 0;
}
