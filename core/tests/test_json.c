/* core/project_json.c on the user's own export (T11's file) and on damaged copies of it: each damage is named by the
 * error it should get.
 *   test_json tests/fixtures/Felucca-project-D-2026-10-24.json */
#include "project_json.h"
#include "check.h"
#include <stdlib.h>
#include <string.h>

static char file[1 << 16], buf[1 << 17];
static fel_project_file_t pf;

/* file with the first `from` replaced by `to` (n times: 0 every one) -> buf */
static const char *edit(const char *from, const char *to, int n)
{
    const char *s = file, *hit;
    char *d = buf;
    size_t lf = strlen(from), lt = strlen(to);
    while ((hit = strstr(s, from)) && n-- != 0) {
        memcpy(d, s, (size_t)(hit - s)); d += hit - s;
        memcpy(d, to, lt); d += lt;
        s = hit + lf;
    }
    strcpy(d, s);
    return buf;
}
static int parse(const char *json) { return fel_project_json(json, strlen(json), &pf); }
#define EXPECT(json, code, what)                                                                   \
    do { int e_ = parse(json); CHECK(e_ == (code), "%s: got \"%s\", expected \"%s\"", what,       \
                                     fel_project_json_error(e_), fel_project_json_error(code)); } while (0)

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
    char data[8192];
    size_t n;
    if (!f) { printf("test_json FILE.json\n"); return 2; }
    n = fread(file, 1, sizeof file - 1, f);
    fclose(f);
    file[n] = 0;

    /* the file as exported */
    EXPECT(file, FEL_PJ_OK, "the user's export");
    CHECK(pf.size == 3840 && pf.crc == 0x642C0757u && pf.slot == 3 && !strcmp(pf.firmware, "FELUCCA v1.6.1") &&
          !memcmp(pf.data, "ANUF", 4), "its contents: %u bytes, CRC %08X, slot %d, \"%s\"", pf.size, pf.crc, pf.slot,
          pf.firmware);

    /* the same in other spellings JSON allows */
    {   /* no whitespace at all */
        char *d = buf; const char *s; int in = 0;
        for (s = file; *s; s++) { if (*s == '"') in = !in; if (in || (*s != ' ' && *s != '\n' && *s != '\r')) *d++ = *s; }
        *d = 0;
        EXPECT(buf, FEL_PJ_OK, "minified");
    }
    EXPECT(edit("/", "\\/", 0), FEL_PJ_OK, "every / escaped as \\/");
    EXPECT(edit("\"slot\"", "\"extra\": {\"a\": [1, -2.5e3, true, null, \"x\\u00e9\"]}, \"slot\"", 1), FEL_PJ_OK,
           "an extra member of any shape");
    EXPECT(edit("\"version\": 1", "\"version\": 2, \"version\": 1", 1), FEL_PJ_OK,
           "a repeated member: the last wins (JSON.parse)");

    /* damaged */
    EXPECT(edit("felucca-project", "felucca-backup", 1), FEL_PJ_FORMAT, "another format");
    EXPECT(edit("\"format\"", "\"formats\"", 1), FEL_PJ_FORMAT, "no format");
    EXPECT(edit("\"version\": 1", "\"version\": 2", 1), FEL_PJ_VERSION, "version 2");
    EXPECT(edit("\"version\": 1", "\"version\": 1.0", 1), FEL_PJ_VERSION, "version 1.0 (not written as an integer)");
    EXPECT(edit("\"object\"", "\"objects\"", 1), FEL_PJ_OBJECT, "no object");
    EXPECT(edit("\"crc\"", "\"crcs\"", 1), FEL_PJ_OBJECT, "no crc");
    EXPECT(edit("\"size\": 3840", "\"size\": 3841", 1), FEL_PJ_SIZE, "size 3841");
    EXPECT(edit("\"size\": 3840", "\"size\": \"3840\"", 1), FEL_PJ_SIZE, "size as a string");
    EXPECT(edit("\"size\": 3840", "\"size\": 3648", 1), FEL_PJ_LENGTH, "size 3648 (FUN9's) for 3840 bytes");
    EXPECT(edit("1680607063", "1680607064", 1), FEL_PJ_CRC, "the CRC off by one");
    EXPECT(edit("\"crc\": 1680607063", "\"crc\": 4294967296", 1), FEL_PJ_OBJECT, "a CRC past 32 bits");
    EXPECT(edit("QU5VRg", "QU5VRw", 1), FEL_PJ_CRC, "one byte of the data changed (still base64)");
    EXPECT(edit("QU5VRg", "QU5V*g", 1), FEL_PJ_BASE64, "a character outside base64");
    EXPECT(edit("QU5VRg", "QU5VR", 1), FEL_PJ_BASE64, "a base64 length not a multiple of 4");
    EXPECT(edit("QU5VRgAP", "QU5V", 1), FEL_PJ_LENGTH, "4 characters short (3 bytes)");
    EXPECT(edit("QU5VRgAP", "QU5V==", 1), FEL_PJ_BASE64, "padding in the middle");
    {
        char cut[4096];
        memcpy(cut, file, 2000); cut[2000] = 0;
        EXPECT(cut, FEL_PJ_SYNTAX, "the file cut short");
    }
    strcpy(buf, file); strcat(buf, " x");
    EXPECT(buf, FEL_PJ_SYNTAX, "something after the object");
    EXPECT(edit("\"slot\": 3", "\"slot\": 03", 1), FEL_PJ_SYNTAX, "a leading zero");
    EXPECT(edit("\"slot\"", "\"slot\" 3, \"x\"", 1), FEL_PJ_SYNTAX, "a member without a colon");
    {
        char deep[512];
        int k;
        for (k = 0; k < 100; k++) deep[k] = '[';
        deep[100] = 0;
        strcpy(buf, "{\"x\": "); strcat(buf, deep);
        EXPECT(buf, FEL_PJ_SYNTAX, "nested 100 deep");
    }
    EXPECT("", FEL_PJ_SYNTAX, "empty");
    EXPECT("[1]", FEL_PJ_SYNTAX, "an array");

    /* an older format's size (FUN7, 3388 bytes: base64 with == padding), made here with its CRC: accepted here (the
     * firmware's proj_import decides whether the bytes are a project) */
    {
        static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        uint8_t raw[3388];
        uint32_t i, k;
        for (i = 0; i < sizeof raw; i++) raw[i] = (uint8_t)(i * 7u + 3u);
        for (i = 0, k = 0; i < sizeof raw; i += 3) {
            uint32_t v = (uint32_t)raw[i] << 16 | (i + 1 < sizeof raw ? (uint32_t)raw[i + 1] << 8 : 0u) |
                         (i + 2 < sizeof raw ? raw[i + 2] : 0u);
            data[k++] = B64[v >> 18 & 63]; data[k++] = B64[v >> 12 & 63];
            data[k++] = i + 1 < sizeof raw ? B64[v >> 6 & 63] : '=';
            data[k++] = i + 2 < sizeof raw ? B64[v & 63] : '=';
        }
        data[k] = 0;
        CHECK(k >= 2 && data[k - 1] == '=' && data[k - 2] == '=', "FUN7's size ends in ==");
        snprintf(buf, sizeof buf, "{\"format\":\"felucca-project\",\"version\":1,\"object\":{\"size\":3388,\"crc\":%u,"
                 "\"data\":\"%s\"}}", fel_crc32(raw, sizeof raw), data);
        EXPECT(buf, FEL_PJ_OK, "FUN7's size, 3388 bytes");
        CHECK(pf.size == 3388 && !memcmp(pf.data, raw, sizeof raw) && pf.slot == -1 && !pf.firmware[0],
              "FUN7's bytes as written, no slot, no firmware");
    }
    return check_done("project JSON (core/project_json.c)");
}
