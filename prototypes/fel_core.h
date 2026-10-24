/* One Felucca core as a table of entry points: every compiled copy of the firmware exports exactly one
 * symbol, fel_core_<n>(), so N copies (N independent instruments) link into one binary. */
#include <stdint.h>
typedef struct {
    void (*boot)(void);
    void (*render)(uint32_t n, float *l, float *r);
    uint32_t (*midi)(uint32_t status, uint32_t d1, uint32_t d2);
    int (*param_set)(int track, int id, int v);
    int (*param_get)(int track, int id);
    int (*engine_find)(const char *name);
    void (*engine_set)(int track, int e);
} fel_core_t;
