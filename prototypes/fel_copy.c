#include "felucca_api.c"
#include "fel_core.h"
static void c_boot(void) { web_nor_erase(); web_master(700); web_boot(); }
static void c_render(uint32_t n, float *l, float *r)
{ web_render(n); memcpy(l, web_out_l(), n * 4); memcpy(r, web_out_r(), n * 4); }
static const fel_core_t CORE = { c_boot, c_render, web_midi, fel_param_set, fel_param_get, fel_engine_find, fel_engine_set };
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
const fel_core_t *CAT(fel_core_, FEL_COPY)(void) { return &CORE; }
/* for the lock-step test: the heavy song playing, a track's position, the device's samples since boot */
void CAT(fel_heavy_, FEL_COPY)(void) { web_test_heavy(); fel_play(1); }
void CAT(fel_pos_, FEL_COPY)(int t, uint32_t *idx, uint32_t *pos) { *idx = trk[t].seq_idx; *pos = trk[t].seq_pos; }
