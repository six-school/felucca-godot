// Audio out (D5): a FeluccaSource's stream and its playback; the playback plays a pool instance while it is bound to one
// (D3, D14: the conductor binds and unbinds them, DESIGN.md §7), and keeps its place in the music without one. It is an
// AudioStreamPlayback of its own rather than an AudioStreamPlaybackResampled: Godot calls _mix once a mix step with
// that step's frames, so they map exactly onto the step's plan (the conductor's timeline from b0 to b1); the resampled
// base would ask for 128-frame chunks at a phase of each player's own, putting instances up to 2.9 ms apart. At a mix
// rate other than the FM-1's 44.1 kHz it resamples itself (cubic).
#pragma once

#include "fel_core.h"
#include "felucca_api.h"
#include "felucca_project.h"

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include <atomic>
#include <vector>

namespace godot {

// where a source's instance is (FeluccaSource.get_snapshot), typed. As rendered: up to a half (128 frames) ahead of what
// has been heard (DESIGN.md §13.12)
class FeluccaSnapshot : public RefCounted {
	GDCLASS(FeluccaSnapshot, RefCounted);

protected:
	static void _bind_methods();

public:
	bool bound = false, playing = false;
	double beats = 0.0, origin = 0.0; // beats since step 1; the conductor's timeline at step 1
	int64_t frames = 0, slot = -1;
	PackedInt32Array steps, positions, loops; // each part's step (0-based), samples into it, passes through its LEN

	bool is_bound() const { return bound; }
	bool is_playing() const { return playing; }
	double get_beats() const { return beats; }
	double get_origin() const { return origin; }
	int64_t get_frames() const { return frames; }
	int64_t get_slot() const { return slot; }
	PackedInt32Array get_steps() const { return steps; }
	PackedInt32Array get_positions() const { return positions; }
	PackedInt32Array get_loops() const { return loops; }
};

class FeluccaStream : public AudioStream {
	GDCLASS(FeluccaStream, AudioStream);

protected:
	static void _bind_methods() {}

public:
	Ref<FeluccaProject> project;
	std::shared_ptr<FelValues> values; // its source's typed values (applied at binding)
	ObjectID source; // the FeluccaSource it is the stream of (its pause, its settings)
	int quantize = 1, join = 0; // FeluccaSource's enums (its defaults: BEAT, FOLLOW)

	Ref<AudioStreamPlayback> _instantiate_playback() const override;
	String _get_stream_name() const override { return "Felucca"; }
	double _get_length() const override { return 0.0; }
	bool _is_monophonic() const override { return true; }
};

// an instance's event, with the mix step it was rendered in (DESIGN.md §6.3: when it is heard)
struct FelHeard {
	fel_event_t e;
	uint64_t step;
};

class FeluccaPlayback : public AudioStreamPlayback {
	GDCLASS(FeluccaPlayback, AudioStreamPlayback);

	// main thread
	bool active = false, registered = false;
	uint64_t mixed = 0;

	// audio thread
	double last_b1 = 0.0, tempo_applied = 0.0;
	bool started = false; // its music has started (step 1 at origin), whether a copy plays it or not
	double rs_pos = 1.0; // the resampler: read position in rs_l / rs_r (frames at FEL_RATE), rs_have of them
	int32_t rs_have = 1;
	std::vector<float> rs_l, rs_r, out_l, out_r;
	// its instance's events (actions applied, dropped): written by _mix (or unbind, under the lock), taken by the
	// conductor under the lock. A fixed ring: past it the newest are lost
	std::vector<FelHeard> heard;
	uint32_t heard_w = 0, heard_r = 0;

	void render(AudioFrame *p_out, int32_t p_frames, double p_b0, double p_b1);
	void keep_events(); // (the instance's events into the ring, stamped with the mix step being mixed)

protected:
	static void _bind_methods() {}

public:
	// set by its stream when made (main thread): the project's bytes, not the resource (a playback still mixing when
	// the game quits is never freed by Godot 4.7: it would keep the resource "in use")
	PackedByteArray data;
	std::weak_ptr<FelValues> values; // (weak: a playback Godot never frees at quit must not keep the resources)
	ObjectID source;
	int quantize = 1, join = 0;
	// main thread: the pool's (FeluccaConductor)
	int slot = -1; // the copy it is bound to, -1 none
	uint64_t seq = 0; // the order of the play()s (the first come binds first while copies are free)
	bool refused = false; // its project did not import: it never binds
	int pins = 0; // its actions pending (scheduled, not heard yet): it keeps an instance (DESIGN.md §7)
	// published under the lock (main thread), read by _mix (audio thread)
	const fel_core_t *core = nullptr;
	bool fresh = false; // bound since the last mix: the copy joins the music (locate) and fades in, in the next one
	bool leaving = false; // evicted: its next mix fades the copy out, the ones after it are silent until it is unbound
	std::atomic<bool> left{ false }; // (written by _mix: that fade is done, the pass may cut the copy)
	bool armed = false; // play() resolved: step 1 at origin
	double origin = 0.0; // the conductor's timeline at its step 1
	bool tree_held = false; // the tree pause holds it (the conductor paused it, or noted its own pause)
	bool tree_told = false; // .. and Godot tells it the pause (it cannot process while the tree is paused)
	bool user_paused = false; // its source was paused by its own stream_paused when the tree paused
	bool hold_silent = false; // .. and Godot's unpause let it go (godot #65743) until the source pauses it again
	int hold_fade = 0; // .. and the mix step after, Godot's fade into that pause (its music must not move in it)

	FeluccaPlayback();
	~FeluccaPlayback();

	void _start(double p_from_pos) override;
	void _stop() override;
	bool _is_playing() const override { return active; }
	double _get_playback_position() const override;
	int32_t _mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) override;

	bool is_bound() const { return core != nullptr; }
	bool is_active() const { return active; } // between Godot's start and stop
	bool wants_copy() const { return active && slot < 0 && !refused && !data.is_empty(); }
	// main thread: the copy in p_slot (free, booted) plays it from the next mix step: the project, the overrides and
	// what scripts set into it, then published under the lock. false: its project did not import (the copy is freed)
	bool bind(int p_slot);
	// main thread: back to playing nothing, under the lock; the copy is cut and freed
	void unbind();
	// main thread: evicted (DESIGN.md §7): its copy fades out over the next mix step; the conductor's pass unbinds it
	// once that is done
	void leave();
	Ref<FeluccaSnapshot> snapshot() const; // the instance's (main thread: takes the lock)
	void take_events(std::vector<FelHeard> &r_out); // main thread, under the lock: the ring's, in order
};

} // namespace godot
