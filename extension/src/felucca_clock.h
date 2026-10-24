// The conductor's per-mix-step hook (D6, T12): an effect on a muted, empty bus that asks to run in silence too. Godot
// mixes every playback first and processes the buses' effects after, so this sees the end of each mix step: B moved
// over it, and the plan of the next one is made here (felucca::Engine::audio_step).
#pragma once

#include <godot_cpp/classes/audio_effect.hpp>
#include <godot_cpp/classes/audio_effect_instance.hpp>

namespace godot {

class FeluccaClockEffectInstance : public AudioEffectInstance {
	GDCLASS(FeluccaClockEffectInstance, AudioEffectInstance);

protected:
	static void _bind_methods() {}

public:
	bool leader = false; // one instance a speaker pair: the first of each set steps the clock

	void _process(const void *p_src_buffer, AudioFrame *r_dst_buffer, int32_t p_frame_count) override;
	bool _process_silence() const override { return true; }
};

class FeluccaClockEffect : public AudioEffect {
	GDCLASS(FeluccaClockEffect, AudioEffect);
	uint32_t instances = 0;

protected:
	static void _bind_methods() {}

public:
	Ref<AudioEffectInstance> _instantiate() override;
};

} // namespace godot
