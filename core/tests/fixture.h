/* An exported Felucca project as the core takes it: its store object's bytes. core/tests/run.sh takes them
 * out of the web app's .json (reading that file is the extension's job, with Godot's own JSON:
 * FeluccaProject::parse_json). */
#include "fel_core.h"
#include <stdio.h>

typedef struct { uint8_t data[FEL_PROJECT_SIZE]; uint32_t size; } fel_fixture_t;

/* path -> 0 and *out filled (up to FEL_PROJECT_SIZE bytes), else 1 */
static int fixture_read(const char *path, fel_fixture_t *out)
{
    FILE *f = path ? fopen(path, "rb") : 0;
    if (!f)
        return 1;
    out->size = (uint32_t)fread(out->data, 1, sizeof out->data, f);
    fclose(f);
    return out->size == 0;
}
