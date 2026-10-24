#include "felucca_api.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

namespace felucca {

const ApiProp *api_prop(const ApiClass &p_class, int p_id) {
	for (int i = 0; i < p_class.nprops; i++) {
		if (p_class.props[i].id == p_id) {
			return &p_class.props[i];
		}
	}
	return nullptr;
}

int api_id(const ApiClass &p_class, const char *p_name) {
	for (int i = 0; i < p_class.nprops; i++) {
		if (!std::strcmp(p_class.props[i].name, p_name)) {
			return p_class.props[i].id;
		}
	}
	return -1;
}

} // namespace felucca

namespace godot {

using namespace felucca;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

struct Locked { // the audio thread is not mixing meanwhile: a bound instance is the main thread's to call
	Locked() { AudioServer::get_singleton()->lock(); }
	~Locked() { AudioServer::get_singleton()->unlock(); }
};

// ---------------------------------------------------------------- the values ---

int32_t FelValues::get_track(int p_track, int p_id, int p_min, int p_max, int p_def) const {
	int32_t v;
	if (core) {
		Locked l;
		return core->param_get(p_track, p_id);
	}
	if (track_set[p_track][p_id]) {
		return track[p_track][p_id];
	}
	if (overrides.is_valid() && overrides->value_of(p_track, p_id, v)) {
		return clampi(v, p_min, p_max);
	}
	if (project.is_valid()) { // (its values hold for its own engines: a part switched to another has that one's)
		PackedInt32Array ids = project->get_engine_ids(), pv = project->get_track_values();
		if (ids.size() == FEL_TRACKS && pv.size() == FEL_TRACKS * FEL_PARAMS && get_engine(p_track) == ids[p_track]) {
			return pv[p_track * FEL_PARAMS + p_id];
		}
	}
	return p_def;
}

void FelValues::set_track(int p_track, int p_id, int32_t p_value, int p_min, int p_max) {
	int32_t v = clampi(p_value, p_min, p_max);
	if (core) {
		Locked l;
		v = core->param_set(p_track, p_id, v);
	}
	track[p_track][p_id] = int16_t(v);
	track_set[p_track][p_id] = 1;
}

int32_t FelValues::get_song(int p_id, int p_def) const {
	if (core) {
		Locked l;
		return core->global_get(p_id);
	}
	if (song_set[p_id]) {
		return song[p_id];
	}
	if (project.is_valid()) {
		PackedInt32Array sv = project->get_song_values();
		if (sv.size() == FEL_GLOBALS) {
			return sv[p_id];
		}
	}
	return p_def;
}

void FelValues::set_song(int p_id, int32_t p_value, int p_min, int p_max) {
	int32_t v = clampi(p_value, p_min, p_max);
	if (core) {
		Locked l;
		v = core->global_set(p_id, v);
	}
	song[p_id] = int16_t(v);
	song_set[p_id] = 1;
}

int32_t FelValues::get_engine(int p_track) const {
	if (core) {
		Locked l;
		return core->engine_get(p_track);
	}
	if (engine_set[p_track]) {
		return engine[p_track];
	}
	if (project.is_valid() && project->get_engine_ids().size() == FEL_TRACKS) {
		return project->get_engine_ids()[p_track];
	}
	return 0;
}

// a new engine: its first preset (the device's ENGINE page), the old engine's eight values forgotten, then the
// part's other values its scripts set again (they win over the preset's)
void FelValues::set_engine(int p_track, int32_t p_engine) {
	for (int v = 0; v < API_VIEW_COUNT; v++) {
		for (int i = 0; i < API_VIEWS[v].nprops; i++) {
			track_set[p_track][API_VIEWS[v].props[i].id] = 0;
		}
	}
	engine[p_track] = p_engine;
	engine_set[p_track] = 1;
	if (core) {
		Locked l;
		engine[p_track] = core->engine_set(p_track, p_engine);
		apply_track(core, p_track);
	}
}

void FelValues::apply_track(const fel_core_t *p_core, int p_track) const {
	for (int id = 0; id < FEL_PARAMS; id++) {
		if (track_set[p_track][id]) {
			p_core->param_set(p_track, id, track[p_track][id]);
		}
	}
}

void FelValues::apply(const fel_core_t *p_core) const {
	for (int t = 0; t < FEL_TRACKS; t++) {
		if (engine_set[t]) {
			p_core->engine_set(t, engine[t]);
		}
	}
	if (overrides.is_valid()) {
		TypedArray<FeluccaOverride> e = overrides->get_entries();
		for (int64_t i = 0; i < e.size(); i++) {
			Ref<FeluccaOverride> o = e[i];
			if (o.is_valid() && o->get_part() >= 1 && o->get_part() <= FEL_TRACKS) {
				p_core->param_set(o->get_part() - 1, o->get_param(), o->get_value());
			}
		}
		static const int mute = api_id(API_PART, "mute");
		for (int t = 0; t < FEL_TRACKS; t++) {
			if ((overrides->get_muted() >> t) & 1) {
				p_core->param_set(t, mute, 1);
			}
		}
	}
	for (int t = 0; t < FEL_TRACKS; t++) {
		apply_track(p_core, t);
	}
	for (int id = 0; id < FEL_GLOBALS; id++) {
		if (song_set[id]) {
			p_core->global_set(id, song[id]);
		}
	}
}

bool FeluccaOverrides::value_of(int p_track, int p_id, int32_t &r_value) const {
	bool found = false;
	for (int64_t i = 0; i < entries.size(); i++) { // (the last for a parameter wins, as applying them in order does)
		Ref<FeluccaOverride> o = entries[i];
		if (o.is_valid() && o->get_part() == p_track + 1 && o->get_param() == p_id) {
			r_value = o->get_value();
			found = true;
		}
	}
	if (p_id == api_id(API_PART, "mute") && ((muted >> p_track) & 1)) {
		r_value = 1;
		found = true;
	}
	return found;
}

// ---------------------------------------------------------------- binding the tables ---

static String enum_class(const ApiClass &c, int e) { return String(c.class_name) + "." + c.enums[e].name; }

// every property of a table, with its enum, as an indexed property of the class: setter(id, v), getter(id)
static void bind_table(const ApiClass &c, const StringName &p_class) {
	for (int e = 0; e < c.nenums; e++) {
		for (int k = 0; k < c.enums[e].count; k++) {
			ClassDB::bind_integer_constant(p_class, c.enums[e].name, c.enums[e].values[k].name, c.enums[e].values[k].value);
		}
	}
	for (int i = 0; i < c.nprops; i++) {
		const ApiProp &p = c.props[i];
		PropertyInfo info = p.enum_index >= 0
				? PropertyInfo(Variant::INT, p.name, PROPERTY_HINT_ENUM, p.hint, PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
						  enum_class(c, p.enum_index))
				: PropertyInfo(Variant::INT, p.name, PROPERTY_HINT_RANGE, p.hint);
		ClassDB::add_property(p_class, info, "_set_value", "_get_value", p.id);
		if (p.fallback) { // (the generated name, for scripts: not shown)
			info.name = p.fallback;
			info.usage = PROPERTY_USAGE_NONE;
			ClassDB::add_property(p_class, info, "_set_value", "_get_value", p.id);
		}
	}
}

// ---------------------------------------------------------------- FeluccaPart ---

void FeluccaPart::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_set_value", "id", "value"), &FeluccaPart::_set_value);
	ClassDB::bind_method(D_METHOD("_get_value", "id"), &FeluccaPart::_get_value);
	ClassDB::bind_method(D_METHOD("set_engine", "engine"), &FeluccaPart::set_engine);
	ClassDB::bind_method(D_METHOD("get_engine"), &FeluccaPart::get_engine);
	ClassDB::bind_method(D_METHOD("_get_view", "view"), &FeluccaPart::_get_view);
	ClassDB::bind_method(D_METHOD("get_index"), &FeluccaPart::get_index);
	bind_table(API_PART, get_class_static());
	String hint;
	for (int k = 0; k < API_ENGINE_ENUM.count; k++) {
		ClassDB::bind_integer_constant(get_class_static(), API_ENGINE_ENUM.name, API_ENGINE_ENUM.values[k].name,
				API_ENGINE_ENUM.values[k].value);
		hint += String(k ? "," : "") + API_ENGINE_ENUM.values[k].name + ":" + String::num_int64(API_ENGINE_ENUM.values[k].value);
	}
	ADD_PROPERTY(PropertyInfo(Variant::INT, "engine", PROPERTY_HINT_ENUM, hint, PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
						 "FeluccaPart.Engine"),
			"set_engine", "get_engine");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "index", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_index");
	for (int k = 0; k < API_PARAM_ENUM.count; k++) {
		ClassDB::bind_integer_constant(get_class_static(), API_PARAM_ENUM.name, API_PARAM_ENUM.values[k].name,
				API_PARAM_ENUM.values[k].value);
	}
	// (one getter a view, returning its own class: GDScript's analyzer types a property by its getter's return)
#define FELUCCA_BIND_VIEW(m_class, m_view, m_engine, m_access)                                                  \
	ClassDB::bind_method(D_METHOD("get_" #m_access), &FeluccaPart::get_##m_access);                           \
	ClassDB::add_property(get_class_static(), PropertyInfo(Variant::OBJECT, #m_access, PROPERTY_HINT_NONE, "", \
													  PROPERTY_USAGE_NONE, #m_class),                      \
			"", "get_" #m_access);
	FELUCCA_ENGINE_VIEWS(FELUCCA_BIND_VIEW)
}

void FeluccaPart::_set_value(int p_id, int32_t p_value) {
	if (const ApiProp *p = api_prop(API_PART, p_id)) {
		values->set_track(track, p_id, p_value, p->min, p->max);
	}
}

int32_t FeluccaPart::_get_value(int p_id) const {
	const ApiProp *p = api_prop(API_PART, p_id);
	return p ? values->get_track(track, p_id, p->min, p->max, p->def) : 0;
}

void FeluccaPart::set_engine(int32_t p_engine) {
	bool known = false;
	for (int k = 0; k < API_ENGINE_ENUM.count; k++) {
		known |= API_ENGINE_ENUM.values[k].value == p_engine;
	}
	if (!known) {
		UtilityFunctions::push_error("FeluccaPart.engine: ", p_engine, " is no engine (FeluccaPart.Engine)");
		return;
	}
	values->set_engine(track, p_engine);
}

int32_t FeluccaPart::get_engine() const {
	return values->get_engine(track);
}

Ref<FeluccaEngineView> FeluccaPart::_get_view(int p_view) {
	if (p_view < 0 || p_view >= API_VIEW_COUNT || get_engine() != API_VIEWS[p_view].engine) {
		return Ref<FeluccaEngineView>();
	}
	if (views[p_view].is_null()) {
		switch (p_view) {
#define FELUCCA_MAKE_VIEW(m_class, m_view, m_engine, m_access) \
	case m_view: {                                             \
		Ref<m_class> v;                                        \
		v.instantiate();                                       \
		views[p_view] = v;                                     \
	} break;
			FELUCCA_ENGINE_VIEWS(FELUCCA_MAKE_VIEW)
		}
		views[p_view]->init(values, track, p_view);
	}
	return views[p_view];
}

// ---------------------------------------------------------------- FeluccaSong ---

void FeluccaSong::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_set_value", "id", "value"), &FeluccaSong::_set_value);
	ClassDB::bind_method(D_METHOD("_get_value", "id"), &FeluccaSong::_get_value);
	bind_table(API_SONG, get_class_static());
}

void FeluccaSong::_set_value(int p_id, int32_t p_value) {
	if (const ApiProp *p = api_prop(API_SONG, p_id)) {
		values->set_song(p_id, p_value, p->min, p->max);
	}
}

int32_t FeluccaSong::_get_value(int p_id) const {
	const ApiProp *p = api_prop(API_SONG, p_id);
	return p ? values->get_song(p_id, p->def) : 0;
}

// ---------------------------------------------------------------- the engine views ---

void FeluccaEngineView::_bind_methods() { // (the views' properties use these: bound once, here)
	ClassDB::bind_method(D_METHOD("_set_value", "id", "value"), &FeluccaEngineView::_set_value);
	ClassDB::bind_method(D_METHOD("_get_value", "id"), &FeluccaEngineView::_get_value);
}

void FeluccaEngineView::bind_view(int p_view) {
	bind_table(API_VIEWS[p_view], API_VIEWS[p_view].class_name);
}

void FeluccaEngineView::_set_value(int p_id, int32_t p_value) {
	const ApiClass &c = API_VIEWS[view];
	if (values->get_engine(track) != c.engine) {
		UtilityFunctions::push_error(c.class_name, ": part ", track + 1, "'s engine is another now");
		return;
	}
	if (const ApiProp *p = api_prop(c, p_id)) {
		values->set_track(track, p_id, p_value, p->min, p->max);
	}
}

int32_t FeluccaEngineView::_get_value(int p_id) const {
	const ApiProp *p = api_prop(API_VIEWS[view], p_id);
	return p ? values->get_track(track, p_id, p->min, p->max, p->def) : 0;
}

// ---------------------------------------------------------------- overrides ---

void FeluccaOverride::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_part", "part"), &FeluccaOverride::set_part);
	ClassDB::bind_method(D_METHOD("get_part"), &FeluccaOverride::get_part);
	ClassDB::bind_method(D_METHOD("set_param", "param"), &FeluccaOverride::set_param);
	ClassDB::bind_method(D_METHOD("get_param"), &FeluccaOverride::get_param);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &FeluccaOverride::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &FeluccaOverride::get_value);
	String hint; // the part's parameters by name (FeluccaPart.Param: their ids): the inspector's dropdown
	for (int i = 0; i < API_PART.nprops; i++) {
		hint += String(i ? "," : "") + API_PART.props[i].name + ":" + String::num_int64(API_PART.props[i].id);
	}
	ADD_PROPERTY(PropertyInfo(Variant::INT, "part", PROPERTY_HINT_RANGE, "1,4"), "set_part", "get_part");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "param", PROPERTY_HINT_ENUM, hint, PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
						 "FeluccaPart.Param"),
			"set_param", "get_param");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "value"), "set_value", "get_value");
}

void FeluccaOverrides::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_entries", "entries"), &FeluccaOverrides::set_entries);
	ClassDB::bind_method(D_METHOD("get_entries"), &FeluccaOverrides::get_entries);
	ClassDB::bind_method(D_METHOD("set_muted", "parts"), &FeluccaOverrides::set_muted);
	ClassDB::bind_method(D_METHOD("get_muted"), &FeluccaOverrides::get_muted);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "entries", PROPERTY_HINT_TYPE_STRING,
						 String::num_int64(Variant::OBJECT) + "/" + String::num_int64(PROPERTY_HINT_RESOURCE_TYPE) + ":FeluccaOverride"),
			"set_entries", "get_entries");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "muted", PROPERTY_HINT_FLAGS, "Part 1,Part 2,Part 3,Part 4"), "set_muted", "get_muted");
}

} // namespace godot
