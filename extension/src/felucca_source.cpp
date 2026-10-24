#include "felucca_source.h"

#include "felucca_conductor.h"
#include "felucca_stream.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

// the stream the player plays: made at run time (in the editor the player's stream stays as it was: nothing of it
// is saved with the scene)
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

Dictionary FelSource::position(Node *p_owner) const {
	FeluccaPlayback *p = playback_of(p_owner);
	if (!p) {
		Dictionary d;
		d["bound"] = false;
		return d;
	}
	return p->position();
}

bool FelSource::bound(Node *p_owner) const {
	FeluccaPlayback *p = playback_of(p_owner);
	return p && p->is_bound();
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
	ClassDB::bind_method(D_METHOD("get_position"), &T::get_position);
	ClassDB::bind_method(D_METHOD("set_overrides", "overrides"), &T::set_overrides);
	ClassDB::bind_method(D_METHOD("get_overrides"), &T::get_overrides);
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
	ClassDB::add_property(c, PropertyInfo(Variant::OBJECT, "project", PROPERTY_HINT_RESOURCE_TYPE, "FeluccaProject"),
			"set_project", "get_project");
	ClassDB::add_property(c, PropertyInfo(Variant::INT, "quantize", PROPERTY_HINT_ENUM, "Now,Beat,Bar"), "set_quantize",
			"get_quantize");
	ClassDB::add_property(c, PropertyInfo(Variant::INT, "join", PROPERTY_HINT_ENUM, "Follow,Hold"), "set_join", "get_join");
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

bool FeluccaSource::is_source(Object *p_object) {
	return Object::cast_to<FeluccaSource>(p_object) || Object::cast_to<FeluccaSource2D>(p_object) ||
			Object::cast_to<FeluccaSource3D>(p_object);
}

} // namespace godot
