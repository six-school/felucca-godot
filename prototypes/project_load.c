/* A web app project export ("Felucca-project-*.json") into a fresh core, as the game will load it: the JSON checked
 * the way the web app checks it (format, version, size, CRC-32 of the bytes), base64 decoded, proj_import +
 * project_restore_runtime; then what it holds, and 16 s of it playing, written as a WAV.
 *   project_load FILE.json [OUT.wav] */
#include "felucca_api.c"
#include <stdio.h>
#include <math.h>

static uint32_t crc32_of(const uint8_t *b, uint32_t n)          /* fm1backup.js bkCrc: CRC-32 (zlib) */
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *b++; for (k = 0; k < 8; k++) c = c >> 1 ^ (c & 1u ? 0xEDB88320u : 0u); }
    return ~c;
}
static int b64(int ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 : ch >= '0' && ch <= '9' ? ch - '0' + 52
         : ch == '+' ? 62 : ch == '/' ? 63 : -1;
}
static const char *field(const char *json, const char *key)     /* the text after "key": (a tiny reader, enough here) */
{
    char pat[64];
    const char *p;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    if (!(p = strstr(json, pat))) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    return p;
}
static void wav(const char *path, const float *lr, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, data = frames * 4, v;
    if (!f) return;
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); v = 1 | 2u << 16; fwrite(&v, 4, 1, f); v = 44100; fwrite(&v, 4, 1, f);
    v = 44100 * 4; fwrite(&v, 4, 1, f); v = 4 | 16u << 16; fwrite(&v, 4, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (i = 0; i < frames * 2; i++) { int32_t s = (int32_t)lrintf(lr[i] * 32767.0f); int16_t s16 = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); fwrite(&s16, 2, 1, f); }
    fclose(f);
}

static char json[1 << 16];
static uint8_t bytes[4096] __attribute__((aligned(16)));   /* proj_import reads it as project_store_t (a union with uint32_t) */
static float audio[44100 * 16 * 2];

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    const char *p;
    uint32_t n = 0, size, crc, acc = 0, bits = 0, i, t, k;
    char name[PROJ_NAME_LEN + 1];
    if (!f) { printf("no file\n"); return 1; }
    json[fread(json, 1, sizeof json - 1, f)] = 0; fclose(f);
    if (!(p = field(json, "format")) || strncmp(p, "\"felucca-project\"", 17)) { printf("not a Felucca project\n"); return 1; }
    p = field(json, "firmware"); printf("file: firmware %.*s\n", p ? (int)(strchr(p + 1, '"') - p - 1) : 0, p ? p + 1 : "");
    size = (uint32_t)strtoul(field(json, "size"), 0, 10);
    crc = (uint32_t)strtoul(field(json, "crc"), 0, 10);
    for (p = field(json, "data") + 1; *p && *p != '"'; p++) {
        int v = b64(*p);
        if (v < 0) continue;                                    /* '=' padding */
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (n < sizeof bytes) bytes[n++] = (uint8_t)(acc >> bits); }
    }
    printf("object: %u bytes (file says %u), CRC-32 %08X (file says %08X): %s; magic %.4s\n", n, size, crc32_of(bytes, n), crc,
           n == size && crc32_of(bytes, n) == crc ? "ok" : "MISMATCH", (char *)bytes);

    web_nor_erase(); web_master(700); web_boot();
    for (k = 0; k < 1500 * 441 / 10; k += 128) web_render(128);
    if (fel_project_import(bytes, (int)n)) { printf("import FAILED\n"); return 1; }
    for (k = 0; k < 100 * 441 / 10; k += 128) web_render(128);
    project_cur_name(name);
    printf("imported: \"%s\", %d BPM, swing %d, song chain %u rows, %u motion records\n", name, song.g[G_BPM],
           song.g[G_SWING], chain_config.count, motion.count);
    for (t = 0; t < NTRK; t++) {
        uint32_t used = 0, notes = 0, hits = 0;
        char div[24]; const char *unit = "";
        for (i = 0; i < NSTEP; i++) {
            used += (trk[t].step[i].n || trk[t].step[i].hit) && i < (uint32_t)trk[t].p[P_SLEN];
            notes += trk[t].step[i].n; hits += __builtin_popcount(trk[t].step[i].hit);
        }
        param_format(&TP[P_SDIV], trk[t].p[P_SDIV], div, &unit);
        printf("  track %u: %-7s preset %2u  LEN %2d DIV %-5s  %2u steps in use (%u notes, %u drum hits)  level %3d%s\n",
               t + 1, ENGINES[trk[t].engine]->name, trk[t].preset, trk[t].p[P_SLEN], div, used, notes, hits,
               trk[t].p[P_LEVEL], trk[t].p[P_MUTE] ? "  MUTED" : "");
    }
    fel_play(1);
    {
        double peak = 0, sq[8] = {0};
        for (k = 0; k < 44100 * 16; k += 128) {
            web_render(128);
            for (i = 0; i < 128 && k + i < 44100 * 16; i++) {      /* (16 s is not a whole number of blocks) */
                float l = web_out_l()[i], r = web_out_r()[i];
                audio[(k + i) * 2] = l; audio[(k + i) * 2 + 1] = r;
                if (fabsf(l) > peak) peak = fabsf(l);
                if (fabsf(r) > peak) peak = fabsf(r);
                sq[(k + i) / (44100 * 2)] += l * l;
            }
        }
        printf("played 16 s: peak %.3f; rms per 2 s:", peak);
        for (i = 0; i < 8; i++) printf(" %.3f", sqrt(sq[i] / (44100 * 2)));
        printf("\n");
    }
    if (argc > 2) { wav(argv[2], audio, 44100 * 16); printf("wrote %s\n", argv[2]); }
    return 0;
}
