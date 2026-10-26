#include "felucca_source.h"

#include "felucca_conductor.h"
#include "felucca_schedule.h"
#include "felucca_stream.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

static FeluccaPlayback *playback_of(Node *p_owner);

// the stream the player plays: made at run time (in the editor the player's stream stays as it was: nothing of it
// is saved with the scene); quantize and join reach the playback playing now too (live: its next resume or play
// resolved uses them)
void FelSource::update(Node *p_owner) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (stream.is_null()) {
		stream.instantiate();
		stream->source = p_owner->get_instance_id();
	}
	stream->project = project;
	stream->values = values;
	stream->quantize = quantize;
	stream->join = join;
	if (Object::cast_to<FeluccaStream>(p_owner->call("get_stream")) != stream.ptr()) {
		p_owner->call("set_stream", stream);
	}
	if (FeluccaPlayback *p = playback_of(p_owner); p && (p->quantize != quantize || p->join != join)) {
		AudioServer::get_singleton()->lock(); // (the audio thread reads them: HOLD's resume)
		p->quantize = quantize;
		p->join = join;
		AudioServer::get_singleton()->unlock();
	}
}

void FelSource::set_overrides(Node *p_owner, const Ref<FeluccaOverrides> &p_overrides) {
	const Callable heard(p_owner, "_on_overrides_changed");
	Ref<FeluccaOverrides> &o = values->overrides;
	if (o.is_valid() && o->is_connected("changed", heard)) {
		o->disconnect("changed", heard);
	}
	o = p_overrides;
	if (o.is_valid() && !o->is_connected("changed", heard)) {
		o->connect("changed", heard);
	}
	values->overrides_changed(); // (another resource: what it sets, and what the old one set back, at once)
}

static FeluccaPlayback *playback_of(Node *p_owner) {
	if (!bool(p_owner->call("has_stream_playback"))) {
		return nullptr;
	}
	Ref<AudioStreamPlayback> p = p_owner->call("get_stream_playback");
	return Object::cast_to<FeluccaPlayback>(p.ptr());
}

void FelSource::notification(Node *p_owner, int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (p_what == Node::NOTIFICATION_ENTER_TREE) {
		if (FeluccaConductor *c = FeluccaConductor::get_singleton()) {
			c->ensure_node();
		}
		update(p_owner);
	} else if (p_what == Node::NOTIFICATION_UNPAUSED) {
		// (after Godot's own handling, which has just set stream_paused = false on it: godot #65743) paused by its own
		// stream_paused before the tree paused: paused again
		FeluccaPlayback *p = playback_of(p_owner);
		if (p && p->hold_silent) {
			AudioServer::get_singleton()->lock();
			p_owner->call("set_stream_paused", true);
			p->hold_silent = false;
			p->hold_fade = 1; // (Godot mixes it once more, fading into the pause)
			p->user_paused = false;
			AudioServer::get_singleton()->unlock();
		}
	}
}

void FelSource::on_set(Node *p_owner, const StringName &p_name) {
	if (p_name == StringName("pitch_scale") || p_name == StringName("doppler_tracking") || p_name == StringName("max_distance")) {
		p_owner->call_deferred("update_configuration_warnings"); // (after the setter: _set comes before it)
	}
}

String FelSource::ignored(const Node *p_owner) {
	String w;
	if (double(p_owner->get("pitch_scale")) != 1.0) {
		w = "pitch_scale has no effect on a Felucca source: its music follows the conductor's tempo (FeluccaConductor.tempo)";
	}
	const Variant d = p_owner->get("doppler_tracking"); // (3D only)
	if (d.get_type() == Variant::INT && int64_t(d) != 0) {
		w += String(w.is_empty() ? "" : "; ") + "Doppler tracking has no effect on a Felucca source (its pitch would be its tempo)";
	}
	return w;
}

PackedStringArray FelSource::warnings(const Node *p_owner) const {
	PackedStringArray a;
	const String w = ignored(p_owner);
	if (!w.is_empty()) {
		a.push_back(w);
	}
	if (const AudioStreamPlayer3D *p = Object::cast_to<AudioStreamPlayer3D>(p_owner); p && p->get_max_distance() <= 0.0f) {
		a.push_back("max_distance is 0 (no limit): this source is always in range, so it always uses an engine while it plays.");
	}
	return a;
}

Ref<FeluccaSnapshot> FelSource::snapshot(Node *p_owner) const {
	FeluccaPlayback *p = playback_of(p_owner);
	if (!p) {
		Ref<FeluccaSnapshot> s;
		s.instantiate();
		return s;
	}
	return p->snapshot();
}

bool FelSource::bound(Node *p_owner) const {
	FeluccaPlayback *p = playback_of(p_owner);
	return p && p->is_bound();
}

// (not the player's is_playing(): false while its own stream_paused holds it; stop() drops the playback)
FeluccaPlayback *FelSource::playing(Node *p_owner) {
	return playback_of(p_owner);
}

Ref<FeluccaSchedule> FelSource::schedule(Node *p_owner, const Ref<FeluccaWhen> &p_when, const Ref<FeluccaAction> &p_action,
		const StringName &p_tag) {
	FeluccaConductor *c = FeluccaConductor::get_singleton();
	FeluccaPlayback *p = playing(p_owner);
	if (!p || !c) {
		UtilityFunctions::push_error(p_owner->get_name(), ": schedule() on a source that is not playing.");
		return Ref<FeluccaSchedule>();
	}
	return c->schedule(p_owner, p, p_when, p_action, p_tag);
}

void FelSource::cancel(Node *p_owner, const StringName &p_tag) {
	if (FeluccaConductor *c = FeluccaConductor::get_singleton()) {
		c->cancel_tag(p_owner, p_tag);
	}
}

Ref<FeluccaPart> FelSource::part(int p_index) {
	if (p_index < 0 || p_index >= FEL_TRACKS) {
		return Ref<FeluccaPart>();
	}
	if (parts[p_index].is_null()) {
		parts[p_index].instantiate();
		parts[p_index]->init(values, p_index);
	}
	return parts[p_index];
}

TypedArray<FeluccaPart> FelSource::all_parts() {
	TypedArray<FeluccaPart> a;
	for (int i = 0; i < FEL_TRACKS; i++) {
		a.push_back(part(i));
	}
	return a;
}

Ref<FeluccaSong> FelSource::get_song() {
	if (song.is_null()) {
		song.instantiate();
		song->init(values);
	}
	return song;
}

template <typename T>
void FelSource::bind() {
	ClassDB::bind_method(D_METHOD("set_project", "project"), &T::set_project);
	ClassDB::bind_method(D_METHOD("get_project"), &T::get_project);
	ClassDB::bind_method(D_METHOD("set_quantize", "quantize"), &T::set_quantize);
	ClassDB::bind_method(D_METHOD("get_quantize"), &T::get_quantize);
	ClassDB::bind_method(D_METHOD("set_join", "join"), &T::set_join);
	ClassDB::bind_method(D_METHOD("get_join"), &T::get_join);
	ClassDB::bind_method(D_METHOD("is_bound"), &T::is_bound);
	ClassDB::bind_method(D_METHOD("get_snapshot"), &T::get_snapshot); // (not get_position: Node2D / 3D's position's)
	ClassDB::bind_method(D_METHOD("set_overrides", "overrides"), &T::set_overrides);
	ClassDB::bind_method(D_METHOD("get_overrides"), &T::get_overrides);
	ClassDB::bind_method(D_METHOD("_on_overrides_changed"), &T::_on_overrides_changed);
	ClassDB::bind_method(D_METHOD("schedule", "when", "action", "tag"), &T::schedule, DEFVAL(StringName()));
	ClassDB::bind_method(D_METHOD("cancel", "tag"), &T::cancel);
	ClassDB::bind_method(D_METHOD("set_pinned", "pinned"), &T::set_pinned);
	ClassDB::bind_method(D_METHOD("is_pinned"), &T::is_pinned);
	ClassDB::bind_method(D_METHOD("set_muted_parts", "parts"), &T::set_muted_parts);
	ClassDB::bind_method(D_METHOD("get_muted_parts"), &T::get_muted_parts);
	ClassDB::bind_method(D_METHOD("_get_part", "index"), &T::_get_part);
	ClassDB::bind_method(D_METHOD("get_parts"), &T::get_parts);
	ClassDB::bind_method(D_METHOD("get_song"), &T::get_song);
	const StringName c = T::get_class_static();
	for (int i = 0; i < FEL_TRACKS; i++) { // (part_1 .. part_4: as the device counts them)
		ClassDB::add_property(c, PropertyInfo(Variant::OBJECT, "part_" + String::num_int64(i + 1), PROPERTY_HINT_NONE, "",
										 PROPERTY_USAGE_NONE, "FeluccaPart"),
				"", "_get_part", i);
	}
	ClassDB::add_property(c, PropertyInfo(Variant::ARRAY, "parts", PROPERTY_HINT_ARRAY_TYPE, "FeluccaPart", PROPERTY_USAGE_NONE),
			"", "get_parts");
	ClassDB::add_property(c, PropertyInfo(Variant::OBJECT, "song", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE, "FeluccaSong"),
			"", "get_song");
	ClassDB::add_property(c, PropertyInfo(Variant::OBJECT, "overrides", PROPERTY_HINT_RESOURCE_TYPE, "FeluccaOverrides"),
			"set_overrides", "get_overrides");
	// (a part sounds only when neither its own mute nor this says it is muted; live)
	ClassDB::add_property(c, PropertyInfo(Variant::INT, "muted_parts", PROPERTY_HINT_FLAGS, "Part 1,Part 2,Part 3,Part 4"),
			"set_muted_parts", "get_muted_parts");
	ClassDB::add_property(c, PropertyInfo(Variant::OBJECT, "project", PROPERTY_HINT_RESOURCE_TYPE, "FeluccaProject"),
			"set_project", "get_project");
	ClassDB::add_property(c, PropertyInfo(Variant::INT, "quantize", PROPERTY_HINT_ENUM, "Now,Beat,Bar"), "set_quantize",
			"get_quantize");
	ClassDB::add_property(c, PropertyInfo(Variant::INT, "join", PROPERTY_HINT_ENUM, "Follow,Hold"), "set_join", "get_join");
	ClassDB::add_property(c, PropertyInfo(Variant::BOOL, "pinned"), "set_pinned", "is_pinned");
	// (M6, DESIGN.md §5.2) an action, when heard; the source got / lost an instance
	ClassDB::add_signal(c, MethodInfo("scheduled", PropertyInfo(Variant::STRING_NAME, "tag"),
								   PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "FeluccaEvent")));
	ClassDB::add_signal(c, MethodInfo("bound"));
	ClassDB::add_signal(c, MethodInfo("unbound"));
}

void FeluccaSource::_bind_methods() {
	FelSource::bind<FeluccaSource>();
	BIND_ENUM_CONSTANT(QUANTIZE_NOW);
	BIND_ENUM_CONSTANT(QUANTIZE_BEAT);
	BIND_ENUM_CONSTANT(QUANTIZE_BAR);
	BIND_ENUM_CONSTANT(JOIN_FOLLOW);
	BIND_ENUM_CONSTANT(JOIN_HOLD);
}

void FeluccaSource2D::_bind_methods() {
	FelSource::bind<FeluccaSource2D>();
}

void FeluccaSource3D::_bind_methods() {
	FelSource::bind<FeluccaSource3D>();
}

FelSource *FelSource::of(Node *p_node) {
	if (FeluccaSource *s = Object::cast_to<FeluccaSource>(p_node)) {
		return &s->fel();
	}
	if (FeluccaSource2D *s = Object::cast_to<FeluccaSource2D>(p_node)) {
		return &s->fel();
	}
	if (FeluccaSource3D *s = Object::cast_to<FeluccaSource3D>(p_node)) {
		return &s->fel();
	}
	return nullptr;
}

bool FeluccaSource::is_source(Object *p_object) {
	return Object::cast_to<FeluccaSource>(p_object) || Object::cast_to<FeluccaSource2D>(p_object) ||
			Object::cast_to<FeluccaSource3D>(p_object);
}

} // namespace godot
