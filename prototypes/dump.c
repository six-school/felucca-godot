/* Dumps the firmware's own parameter metadata as JSON: what a code generator turns into typed Godot bindings. */
#include "felucca_api.c"
#include <stdio.h>
static void jstr(const char *s) { putchar('"'); for (; s && *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); } putchar('"'); }
static void desc(const param_desc_t *d)
{
    int v;
    printf("\"label\":"); jstr(d->label);
    printf(",\"fmt\":%d,\"min\":%d,\"max\":%d,\"def\":%d", d->fmt, d->min, d->max, d->def);
    if (d->fmt == F_ENUM && d->names) {
        printf(",\"names\":[");
        for (v = d->min; v <= d->max; v++) { if (v > d->min) putchar(','); jstr(d->names[v]); }
        putchar(']');
    }
}
int main(void)
{
    uint32_t i, k, e, first = 1;
    printf("{\"version\":\"%s\",\"pages\":[", FELUCCA_VERSION);
    for (i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++) {
        if (PAGES[i].scope == SC_ENGINE) continue;
        printf("%s{\"page\":", first ? "" : ","); first = 0; jstr(PAGES[i].title);
        printf(",\"scope\":\"%s\",\"params\":[", PAGES[i].scope == SC_GLOBAL ? "global" : "track");
        for (k = 0, e = 0; k < 4; k++) {
            uint8_t id = PAGES[i].id[k];
            if (id == 0xFF) continue;
            printf("%s{\"id\":%u,", e++ ? "," : "", id);
            desc(PAGES[i].scope == SC_GLOBAL ? &GP[id] : &TP[id]);
            putchar('}');
        }
        printf("]}");
    }
    printf("],\"engines\":[");
    for (e = 0; e < NENGINES; e++) {
        printf("%s{\"name\":", e ? "," : ""); jstr(ENGINES[e]->name); printf(",\"params\":[");
        for (k = 0; k < 8; k++) { printf("%s{\"id\":%u,", k ? "," : "", P_E0 + k); desc(&ENGINES[e]->edit[k]); putchar('}'); }
        printf("]}");
    }
    printf("]}\n");
    return 0;
}
