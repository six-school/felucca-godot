/* A Felucca web app project export ("Felucca-project-*.json", D13) -> the store object fel_core_t.project_import
 * takes. Firmware-independent (compiled once, not per copy): the game's importer and the C tests share it.
 *   {"format": "felucca-project", "version": 1, "firmware": "FELUCCA v1.6.1", "slot": 3,
 *    "object": {"size": 3840, "crc": <CRC-32 (zlib) of the bytes, unsigned>, "data": "<base64 of the bytes>"}}
 * Checked as the web app writes it: the format, version 1, a size the firmware's backup path accepts (FUN10 3840,
 * FUN9 3648, FUN8 3584, FUN7 3388: editor_backup.c), strict base64 of exactly that many bytes, the CRC. Whether the
 * bytes are a project is the firmware's to say (proj_import: magic, hash, engines) when it is imported. */
#ifndef FEL_PROJECT_JSON_H
#define FEL_PROJECT_JSON_H
#include <stddef.h>
#include <stdint.h>
#include "fel_core.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t data[FEL_PROJECT_SIZE];     /* the store object, size bytes */
    uint32_t size, crc;
    int32_t slot;                       /* the device slot it came from (information), -1 if the file has none */
    char firmware[32];                  /* "FELUCCA v1.6.1" (information, cut to fit), "" if none */
} fel_project_file_t;

enum {
    FEL_PJ_OK, FEL_PJ_SYNTAX, FEL_PJ_FORMAT, FEL_PJ_VERSION, FEL_PJ_OBJECT, FEL_PJ_SIZE, FEL_PJ_BASE64, FEL_PJ_LENGTH,
    FEL_PJ_CRC
};

/* json: n bytes (no terminator needed) -> FEL_PJ_OK and *out filled, or the first problem found */
int fel_project_json(const char *json, size_t n, fel_project_file_t *out);
const char *fel_project_json_error(int code);
uint32_t fel_crc32(const uint8_t *b, size_t n);         /* CRC-32 (zlib), as the web app's backup code computes it */

#ifdef __cplusplus
}
#endif

#endif
