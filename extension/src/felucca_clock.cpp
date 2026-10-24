#include "felucca_clock.h"

#include "felucca_engine.h"

#include <godot_cpp/classes/audio_server.hpp>

namespace godot {

void FeluccaClockEffectInstance::_process(const void *p_src_buffer, AudioFrame *r_dst_buffer, int32_t p_frame_count) {
	const AudioFrame *src = static_cast<const AudioFrame *>(p_src_buffer);
	for (int32_t i = 0; i < p_frame_count; i++) {
		r_dst_buffer[i] = src[i];
	}
	if (leader) {
		felucca::Engine::get().audio_step(uint32_t(p_frame_count));
	}
}

Ref<AudioEffectInstance> FeluccaClockEffect::_instantiate() {
	// a bus has one channel a speaker pair (stereo 1 .. 7.1 4), each with its own instance, made in order: the
	// first of each set leads, so the clock steps once a mix step whatever the speaker mode
	uint32_t channels = uint32_t(AudioServer::get_singleton()->get_speaker_mode()) + 1;
	Ref<FeluccaClockEffectInstance> i;
	i.instantiate();
	i->leader = instances++ % channels == 0;
	return i;
}

} // namespace godot
