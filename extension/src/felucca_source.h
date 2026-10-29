// Sources: FeluccaSource, FeluccaSource2D, FeluccaSource3D are the AudioStreamPlayer* nodes that play a
// project. Their own play() / stop() / stream_paused as for any player; the stream is theirs (made from project,
// quantize, join at run time), each playback one pool instance while it is in range of the listener (the conductor's
// binding pass).
#pragma once

#include "felucca_api.h"
#include "felucca_project.h"

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>

namespace godot {

class FeluccaStream;
class FeluccaSnapshot;
class FeluccaPlayback;
class FeluccaWhen;
class FeluccaAction;
class FeluccaSchedule;

// what the three share
struct FelSource {
	Ref<FeluccaProject> project;
	int quantize = 1, join = 0; // QUANTIZE_BEAT (step 1 on a beat: a music engine's default), JOIN_FOLLOW
	Ref<FeluccaStream> stream;
	std::shared_ptr<FelValues> values = std::make_shared<FelValues>(); // (its parts', its song's, its playbacks')
	Ref<FeluccaPart> parts[FEL_TRACKS];
	Ref<FeluccaSong> song;
	bool pinned = false; // it always has an instance while it plays, as with actions pending
	bool reported = false; // bound, as its last bound / unbound signal said (the conductor's)

	Ref<FeluccaPart> part(int p_index);
	Ref<FeluccaSong> get_song();
	TypedArray<FeluccaPart> all_parts();

	void update(Node *p_owner);
	void set_overrides(Node *p_owner, const Ref<FeluccaOverrides> &p_overrides); // (live: their changes heard)
	void notification(Node *p_owner, int p_what);
	// the player's properties a Felucca source does not follow (its music is the conductor's: FeluccaConductor.tempo):
	// a node warning in the editor, refreshed when one is set; a warning when it starts playing with them. And in the
	// editor only, a 3D source without a max_distance: always in range, it always takes a copy while it plays
	void on_set(Node *p_owner, const StringName &p_name);
	PackedStringArray warnings(const Node *p_owner) const;
	static String ignored(const Node *p_owner); // "" none, else what it ignores
	Ref<FeluccaSnapshot> snapshot(Node *p_owner) const;
	bool bound(Node *p_owner) const;
	// an action held until its target, on the source while it plays (FeluccaConductor::schedule), -> its handle,
	// or null (an error said why); cancel: the pending ones with that tag
	Ref<FeluccaSchedule> schedule(Node *p_owner, const Ref<FeluccaWhen> &p_when, const Ref<FeluccaAction> &p_action,
			const StringName &p_tag, const Callable &p_callback);
	Ref<FeluccaSnapshot> rendered_snapshot(Node *p_owner) const; // (hidden: the tests')
	void cancel(Node *p_owner, const StringName &p_tag);
	static FelSource *of(Node *p_node); // a Felucca source's, else nullptr
	static FeluccaPlayback *playing(Node *p_owner); // its playback while it plays, paused too (stopped: nullptr)
	template <typename T>
	static void bind();
};

#define FELUCCA_SOURCE_BODY(m_class)                                                                \
	FelSource src;                                                                                  \
                                                                                                    \
protected:                                                                                          \
	static void _bind_methods();                                                                    \
	void _notification(int p_what) { src.notification(this, p_what); }                              \
                                                                                                    \
public:                                                                                             \
	void set_project(const Ref<FeluccaProject> &p_project) {                                        \
		src.project = p_project;                                                                    \
		src.values->project = p_project;                                                            \
		src.update(this);                                                                           \
	}                                                                                               \
	void set_overrides(const Ref<FeluccaOverrides> &p_v) { src.set_overrides(this, p_v); }          \
	Ref<FeluccaOverrides> get_overrides() const { return src.values->overrides; }                   \
	void _on_overrides_changed() { src.values->overrides_changed(); }                               \
	void set_muted_parts(int p_v) { src.values->set_muted(p_v); }                                   \
	int get_muted_parts() const { return src.values->muted; }                                       \
	/* (a property set through Godot's: pitch_scale's or doppler_tracking's warning refreshed) */   \
	bool _set(const StringName &p_name, const Variant &p_value) {                                   \
		src.on_set(this, p_name);                                                                   \
		return false;                                                                               \
	}                                                                                               \
	PackedStringArray _get_configuration_warnings() const override { return src.warnings(this); }   \
	Ref<FeluccaPart> _get_part(int p_index) { return src.part(p_index); }                           \
	TypedArray<FeluccaPart> get_parts() { return src.all_parts(); }                                 \
	Ref<FeluccaSong> get_song() { return src.get_song(); }                                          \
	Ref<FeluccaProject> get_project() const { return src.project; }                                 \
	void set_quantize(int p_v) {                                                                    \
		src.quantize = p_v;                                                                         \
		src.update(this);                                                                           \
	}                                                                                               \
	int get_quantize() const { return src.quantize; }                                               \
	void set_join(int p_v) {                                                                        \
		src.join = p_v;                                                                             \
		src.update(this);                                                                           \
	}                                                                                               \
	int get_join() const { return src.join; }                                                       \
	bool is_bound() { return src.bound(this); }                                                     \
	Ref<FeluccaSnapshot> get_snapshot() { return src.snapshot(this); }                              \
	Ref<FeluccaSchedule> schedule(const Ref<FeluccaWhen> &p_when, const Ref<FeluccaAction> &p_action, \
			const Callable &p_callback) { return src.schedule(this, p_when, p_action, StringName(), p_callback); } \
	Ref<FeluccaSchedule> schedule_with_tag(const Ref<FeluccaWhen> &p_when, const Ref<FeluccaAction> &p_action, \
			const StringName &p_tag, const Callable &p_callback) {                                  \
		return src.schedule(this, p_when, p_action, p_tag, p_callback);                             \
	}                                                                                               \
	Ref<FeluccaSnapshot> _get_rendered_snapshot() { return src.rendered_snapshot(this); }           \
	void cancel(const StringName &p_tag) { src.cancel(this, p_tag); }                               \
	void set_pinned(bool p_v) { src.pinned = p_v; }                                                 \
	bool is_pinned() const { return src.pinned; }                                                   \
	FelSource &fel() { return src; } /* (the extension's own) */

class FeluccaSource : public AudioStreamPlayer {
	GDCLASS(FeluccaSource, AudioStreamPlayer);
	FELUCCA_SOURCE_BODY(FeluccaSource)

public:
	// where step 1 goes when play() is called: the conductor's timeline where the next mix step starts, or that rounded
	// up to a beat / a bar
	enum Quantize { QUANTIZE_NOW, QUANTIZE_BEAT, QUANTIZE_BAR };
	// on resuming after its own stream_paused: back where the music is (FOLLOW) or on from where it stopped (HOLD: its
	// origin later by the pause, rounded to the nearest beat or bar as it quantizes; exactly with QUANTIZE_NOW)
	enum Join { JOIN_FOLLOW, JOIN_HOLD };
	static bool is_source(Object *p_object);
};

class FeluccaSource2D : public AudioStreamPlayer2D {
	GDCLASS(FeluccaSource2D, AudioStreamPlayer2D);
	FELUCCA_SOURCE_BODY(FeluccaSource2D)
};

class FeluccaSource3D : public AudioStreamPlayer3D {
	GDCLASS(FeluccaSource3D, AudioStreamPlayer3D);
	FELUCCA_SOURCE_BODY(FeluccaSource3D)
};

} // namespace godot

VARIANT_ENUM_CAST(FeluccaSource::Quantize);
VARIANT_ENUM_CAST(FeluccaSource::Join);
