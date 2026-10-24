// Audio out (D5): a FeluccaSource's stream and its playback, one pool instance each (D3). The playback is an
// AudioStreamPlayback of its own rather than an AudioStreamPlaybackResampled: Godot calls _mix once a mix step with
// that step's frames, so they map exactly onto the step's plan (B from b0 to b1); the resampled base would ask for
// 128-frame chunks at a phase of each player's own, putting instances up to 2.9 ms apart. At a mix rate other than
// the FM-1's 44.1 kHz it resamples itself (cubic).
#pragma once

#include "fel_core.h"
#include "felucca_project.h"

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>

#include <vector>

namespace godot {

class FeluccaStream : public AudioStream {
	GDCLASS(FeluccaStream, AudioStream);

protected:
	static void _bind_methods() {}

public:
	Ref<FeluccaProject> project;
	ObjectID source; // the FeluccaSource it is the stream of (its pause, its settings)
	int quantize = 0, join = 0; // FeluccaSource's enums

	Ref<AudioStreamPlayback> _instantiate_playback() const override;
	String _get_stream_name() const override { return "Felucca"; }
	double _get_length() const override { return 0.0; }
	bool _is_monophonic() const override { return true; }
};

class FeluccaPlayback : public AudioStreamPlayback {
	GDCLASS(FeluccaPlayback, AudioStreamPlayback);

	// main thread
	bool active = false;
	int slot = -1;
	uint64_t mixed = 0;

	// audio thread
	double last_b1 = 0.0, tempo_applied = 0.0;
	bool started = false;
	double rs_pos = 1.0; // the resampler: read position in rs_l / rs_r (frames at FEL_RATE), rs_have of them
	int32_t rs_have = 1;
	std::vector<float> rs_l, rs_r, out_l, out_r;

	void render(AudioFrame *p_out, int32_t p_frames, double p_b0, double p_b1);

protected:
	static void _bind_methods() {}

public:
	// set by its stream when made (main thread): the project's bytes, not the resource (a playback still mixing when
	// the game quits is never freed by Godot 4.7: it would keep the resource "in use")
	PackedByteArray data;
	ObjectID source;
	int quantize = 0, join = 0;
	// published under the lock (main thread), read by _mix (audio thread)
	const fel_core_t *core = nullptr;
	bool armed = false; // play() resolved: step 1 at origin
	double origin = 0.0; // B of its step 1
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
	Dictionary position() const; // the instance's snapshot (main thread: takes the lock)
};

} // namespace godot
