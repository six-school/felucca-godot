// The typed API: FeluccaPart (a source's part: every track parameter, its engine and engine views),
// FeluccaSong (its sound's globals), one FeluccaEngineView an engine (FeluccaAnalog ..), FeluccaOverrides. The
// properties come from tables extension/codegen/gen_api.py makes of the firmware's own (felucca_api.gen.cpp): each
// is a real ClassDB property, so GDScript's analyzer knows it, its type and its enum (a typo is a parse error).
//
// Values: what a source's scripts set is kept on the source (FelValues), applied at once to the instance that
// plays it (under AudioServer::lock(), as the audio thread is then not mixing) and at binding to the next one; read
// back, a bound part gives what sounds (recorded automation included), an unbound one what was set, else what its
// overrides and its project say. A part's value: its project's, under its overrides' (FeluccaOverrides, live: a
// change reaches a bound instance at once), under what scripts set; its mute besides OR'd with the source's own
// muted_parts (a part sounds only when neither says it is muted).
#pragma once

#include "fel_core.h"
#include "felucca_api.gen.h"
#include "felucca_project.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdint>
#include <memory>

namespace felucca {

struct ApiConst {
	const char *name;
	int value;
};
struct ApiEnum {
	const char *name;
	const ApiConst *values;
	int count;
};
struct ApiProp {
	const char *name; // the property
	const char *fallback; // the generated name names.json renamed (a hidden property), or nullptr
	int id; // the parameter id (track or global)
	int min, max, def;
	int enum_index; // into its class's enums, -1 none
	const char *hint; // PROPERTY_HINT_ENUM's "LABEL:value,..", or the range "min,max"
};
struct ApiClass {
	const char *class_name;
	const ApiProp *props;
	int nprops;
	const ApiEnum *enums;
	int nenums;
	int engine; // an engine view's engine index, -1 else
};
extern const ApiClass API_PART, API_SONG, API_VIEWS[];
extern const int API_VIEW_COUNT;
extern const ApiEnum API_ENGINE_ENUM, API_PARAM_ENUM, API_LANE_ENUM;
extern const char *const API_FELUCCA;

const ApiProp *api_prop(const ApiClass &p_class, int p_id); // the property of id, nullptr none
int api_id(const ApiClass &p_class, const char *p_name); // the id of a property, -1 none

} // namespace felucca

namespace godot {

class FeluccaOverrides;

// what a source's scripts set, shared by the source, its parts and whatever playback binds an instance for it
struct FelValues {
	const fel_core_t *core = nullptr; // the instance playing the source now (main thread)
	Ref<FeluccaProject> project;
	Ref<FeluccaOverrides> overrides;
	int16_t track[FEL_TRACKS][FEL_PARAMS] = {};
	uint8_t track_set[FEL_TRACKS][FEL_PARAMS] = {};
	int16_t song[FEL_GLOBALS] = {};
	uint8_t song_set[FEL_GLOBALS] = {};
	int32_t engine[FEL_TRACKS] = {};
	uint8_t engine_set[FEL_TRACKS] = {};
	int muted = 0; // the source's muted_parts: bit t, part t + 1

	int32_t get_track(int p_track, int p_id, int p_min, int p_max, int p_def) const;
	void set_track(int p_track, int p_id, int32_t p_value, int p_min, int p_max);
	// a scheduled set (FeluccaAction.set_param) has applied in the instance: kept as what scripts set (a binding
	// after plays it), the source's muted_parts still over a mute
	void applied_track(int p_track, int p_id, int32_t p_value);
	int32_t get_song(int p_id, int p_def) const;
	void set_song(int p_id, int32_t p_value, int p_min, int p_max);
	int32_t get_engine(int p_track) const;
	void set_engine(int p_track, int32_t p_engine);
	// at binding, on the main thread before the instance is mixed: engines, overrides, then what scripts set, the
	// source's muted_parts
	void apply(const fel_core_t *p_core);
	// main thread: the source's muted_parts changed (bound: the instance's mutes at once)
	void set_muted(int p_parts);
	// main thread: the overrides changed, or another resource took their place (bound: what they set now, and what
	// they no longer set back to the project's, at once; never what scripts set)
	void overrides_changed();

private:
	int16_t ov_val[FEL_TRACKS][FEL_PARAMS] = {}; // what the overrides set when last applied
	uint8_t ov_on[FEL_TRACKS][FEL_PARAMS] = {};
	void apply_track(const fel_core_t *p_core, int p_track) const;
	void overrides_now(int16_t r_val[FEL_TRACKS][FEL_PARAMS], uint8_t r_on[FEL_TRACKS][FEL_PARAMS]) const;
	bool project_value(int p_track, int p_id, int32_t &r_v) const; // (its values hold for its own engines only)
	int32_t muted_value(int p_track, int p_id, int32_t p_v) const { return p_id == mute_id() && ((muted >> p_track) & 1) ? 1 : p_v; }
	static int mute_id();
};

// a part's engine (the engine's eight parameters), while it is the part's engine: set otherwise, it says so
class FeluccaEngineView : public RefCounted {
	GDCLASS(FeluccaEngineView, RefCounted);

protected:
	std::shared_ptr<FelValues> values = std::make_shared<FelValues>(); // (a source's, once init: alone, defaults)
	int track = 0, view = 0;
	static void _bind_methods();
	static void bind_view(int p_view);

public:
	void init(const std::shared_ptr<FelValues> &p_values, int p_track, int p_view) {
		values = p_values;
		track = p_track;
		view = p_view;
	}
	void _set_value(int p_id, int32_t p_value);
	int32_t _get_value(int p_id) const;
};

#define FELUCCA_DECLARE_VIEW(m_class, m_view, m_engine, m_access) \
	class m_class : public FeluccaEngineView {                   \
		GDCLASS(m_class, FeluccaEngineView);                     \
                                                                 \
	protected:                                                   \
		static void _bind_methods() { bind_view(m_view); }       \
	};
FELUCCA_ENGINE_VIEWS(FELUCCA_DECLARE_VIEW)

class FeluccaPart : public RefCounted {
	GDCLASS(FeluccaPart, RefCounted);
	std::shared_ptr<FelValues> values = std::make_shared<FelValues>(); // (a source's, once init: alone, defaults)
	int track = 0;
	Ref<FeluccaEngineView> views[16];

protected:
	static void _bind_methods();

public:
	void init(const std::shared_ptr<FelValues> &p_values, int p_track) {
		values = p_values;
		track = p_track;
	}
	void _set_value(int p_id, int32_t p_value);
	int32_t _get_value(int p_id) const;
	void set_engine(int32_t p_engine);
	int32_t get_engine() const;
	Ref<FeluccaEngineView> _get_view(int p_view);
#define FELUCCA_VIEW_GETTER(m_class, m_view, m_engine, m_access) \
	Ref<m_class> get_##m_access() { return _get_view(m_view); }
	FELUCCA_ENGINE_VIEWS(FELUCCA_VIEW_GETTER)
	int get_index() const { return track + 1; } // 1 .. 4, as on the device
	// the extension's own: whose part it is (FeluccaWhen, FeluccaAction), which
	const std::shared_ptr<FelValues> &source_values() const { return values; }
	int get_track() const { return track; }
};

class FeluccaSong : public RefCounted {
	GDCLASS(FeluccaSong, RefCounted);
	std::shared_ptr<FelValues> values = std::make_shared<FelValues>(); // (a source's, once init: alone, defaults)

protected:
	static void _bind_methods();

public:
	void init(const std::shared_ptr<FelValues> &p_values) { values = p_values; }
	void _set_value(int p_id, int32_t p_value);
	int32_t _get_value(int p_id) const;
};


// one override: a part's parameter set to a value over its project's, at binding and live (a source hears its
// overrides' changes); to mute parts, a source's muted_parts
class FeluccaOverride : public Resource {
	GDCLASS(FeluccaOverride, Resource);
	int part = 1, param = 0, value = 0;

protected:
	static void _bind_methods();

public:
	void set_part(int p_v) {
		part = p_v;
		emit_changed();
	}
	int get_part() const { return part; }
	void set_param(int p_v) {
		param = p_v;
		emit_changed();
	}
	int get_param() const { return param; }
	void set_value(int p_v) {
		value = p_v;
		emit_changed();
	}
	int get_value() const { return value; }
};

// a source's overrides: a list of them, shareable between sources (a .tres)
class FeluccaOverrides : public Resource {
	GDCLASS(FeluccaOverrides, Resource);
	TypedArray<FeluccaOverride> entries;

protected:
	static void _bind_methods();

public:
	void set_entries(const TypedArray<FeluccaOverride> &p_v); // (an entry's change is the whole's: its sources hear it)
	TypedArray<FeluccaOverride> get_entries() const { return entries; }
	// the value it gives part p_track's p_id (true), or none
	bool value_of(int p_track, int p_id, int32_t &r_value) const;
};

} // namespace godot
