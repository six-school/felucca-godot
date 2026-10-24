/* A sketch of the API a Godot GDExtension (C++ or godot-rust FFI) would wrap: Felucca's browser build
 * (web/emu/felucca_web.c, unmodified) compiled natively, plus direct parameter access by name. The writes follow
 * editor.c's ED_TRACK_PARAM / ED_SET (clamp to the descriptor, enum alias, motion base), not the panel. */
#include "web/emu/felucca_web.c"

/* ---- parameters ---- */
EXPORT int fel_param_find(int track, const char *label)   /* a track parameter by its label ("CUT", "RATE"..) */
{
    uint32_t id;
    if (track < 0 || track >= NTRK) return -1;
    for (id = 0; id < P_COUNT; id++) {
        const param_desc_t *d = track_desc(&trk[track], id);
        if (d && d->label && !strcmp(d->label, label)) return (int)id;
    }
    return -1;
}
EXPORT int fel_param_range(int track, int id, int *min, int *max)
{
    const param_desc_t *d;
    if (track < 0 || track >= NTRK || id < 0 || id >= P_COUNT) return -1;
    d = track_desc(&trk[track], (uint32_t)id);
    *min = d->min; *max = d->max;
    return 0;
}
EXPORT const char *fel_param_label(int track, int id)
{
    if (track < 0 || track >= NTRK || id < 0 || id >= P_COUNT) return "";
    return track_desc(&trk[track], (uint32_t)id)->label;
}
EXPORT int fel_param_get(int track, int id) { return trk[track].p[id]; }
EXPORT int fel_param_set(int track, int id, int v)           /* -> the value it took */
{
    track_t *t;
    const param_desc_t *d;
    if (track < 0 || track >= NTRK || id < 0 || id >= P_COUNT) return 0;
    t = &trk[track];
    d = track_desc(t, (uint32_t)id);
    if (d->max > d->min)
        t->p[id] = (int16_t)enum_orig(d, clamp(v, d->min, d->max));
    (void)motion_capture(t, (uint32_t)id, t->p[id]);          /* the motion sequencer's base follows */
    ui.force = 1;
    return t->p[id];
}
EXPORT int fel_global_set(int id, int v)
{
    const param_desc_t *d;
    if (id < 0 || id >= G_COUNT) return 0;
    d = &GP[id];
    if (d->max > d->min) song.g[id] = (int16_t)clamp(v, d->min, d->max);
    ui.force = 1;
    return song.g[id];
}
EXPORT int fel_engine_find(const char *name)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++) if (!strcmp(ENGINES[e]->name, name)) return (int)e;
    return -1;
}
EXPORT void fel_engine_set(int track, int e) { set_engine_of(&trk[track], (uint32_t)e); }

/* ---- transport ---- */
EXPORT void fel_play(int on) { if (on && !song.playing) seq_start(); else if (!on && song.playing) seq_stop(); }

/* ---- projects: the 3840-byte object inside the web editor's Felucca-project-*.json (base64) ---- */
static project_t fel_proj;
static project_store_t fel_store;
EXPORT int fel_project_export(uint8_t *out)                  /* -> bytes written */
{
    project_capture(&fel_proj);
    if (!proj_pack(&fel_store, &fel_proj)) return 0;
    memcpy(out, fel_store.raw, PROJ_STORE_SIZE);
    return PROJ_STORE_SIZE;
}
EXPORT int fel_project_import(const uint8_t *b, int n)       /* 0: loaded */
{
    if (!proj_import(&fel_proj, b, n)) return 1;
    return project_restore_runtime(&fel_proj);
}
