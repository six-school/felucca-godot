#include "felucca_stream.h"

#include "felucca_conductor.h"
#include "felucca_engine.h"
#include "felucca_source.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

namespace godot {

static const int32_t CHUNK = 4096; // frames a render (the scratch buffers' size: none is allocated while mixing)

Ref<AudioStreamPlayback> FeluccaStream::_instantiate_playback() const {
	Ref<FeluccaPlayback> p;
	p.instantiate();
	if (project.is_valid()) {
		p->data = project->get_data();
	}
	p->values = values;
	p->source = source;
	p->quantize = quantize;
	p->join = join;
	return p;
}

FeluccaPlayback::FeluccaPlayback() {
	rs_l.assign(size_t(2 * CHUNK + 8), 0.0f);
	rs_r.assign(size_t(2 * CHUNK + 8), 0.0f);
	out_l.assign(size_t(2 * CHUNK + 8), 0.0f);
	out_r.assign(size_t(2 * CHUNK + 8), 0.0f);
}

// main thread, after Godot has taken it out of the mix (AudioServer::update frees deleted playbacks there): its copy,
// if it has one, is cut and free for the next source (no lock: nothing mixes this playback any more)
FeluccaPlayback::~FeluccaPlayback() {
	if (FeluccaConductor *c = FeluccaConductor::get_singleton()) {
		c->remove_playback(this);
	}
	if (std::shared_ptr<FelValues> v = values.lock(); v && v->core == core) {
		v->core = nullptr;
	}
	if (slot >= 0) {
		felucca::Engine::get().release(slot);
	}
}

// main thread, before Godot adds it to the mix (AudioServer::start_playback_stream): the conductor resolves this
// frame's play()s to one origin (resolve_pending) and gives free copies to the playbacks waiting for one (bind_waiting),
// both last in the frame's process; until it has a copy, it keeps its place in the music silently
void FeluccaPlayback::_start(double p_from_pos) {
	active = true;
	if (registered) {
		return;
	}
	if (data.is_empty()) {
		UtilityFunctions::push_warning("FeluccaSource: no project to play");
		return;
	}
	registered = true;
	if (FeluccaConductor *fc = FeluccaConductor::get_singleton()) {
		fc->add_playback(this);
	}
	if (const Node *n = Object::cast_to<Node>(ObjectDB::get_instance(source))) {
		if (String w = FelSource::ignored(n); !w.is_empty()) {
			UtilityFunctions::push_warning(n->get_name(), ": ", w);
		}
	}
}

bool FeluccaPlayback::bind(int p_slot) {
	felucca::Engine &e = felucca::Engine::get();
	const fel_core_t *c = e.core(p_slot);
	if (!c || c->project_import(data.ptr(), uint32_t(data.size())) != 0) {
		UtilityFunctions::push_error("FeluccaSource: the instance refused the project");
		e.release(p_slot);
		refused = true;
		return false;
	}
	std::shared_ptr<FelValues> v = values.lock();
	if (v) {
		v->apply(c); // the source's overrides and what its scripts set (the copy is not mixed: no lock needed)
	}
	slot = p_slot;
	AudioServer::get_singleton()->lock();
	core = c;
	fresh = true;
	tempo_applied = 0.0;
	rs_pos = 1.0; // (the resampler from silence: nothing of what it held before)
	rs_have = 1;
	rs_l[0] = rs_r[0] = 0.0f;
	if (v) {
		v->core = c; // (from now its parts write through to c)
	}
	AudioServer::get_singleton()->unlock();
	return true;
}

void FeluccaPlayback::unbind() {
	if (slot < 0) {
		return;
	}
	AudioServer::get_singleton()->lock();
	if (std::shared_ptr<FelValues> v = values.lock(); v && v->core == core) {
		v->core = nullptr;
	}
	core = nullptr;
	fresh = false;
	felucca::Engine::get().release(slot); // (cut: under the lock, as the audio thread is not mixing)
	AudioServer::get_singleton()->unlock();
	slot = -1;
}

void FeluccaPlayback::_stop() {
	active = false;
}

double FeluccaPlayback::_get_playback_position() const {
	return double(mixed) / felucca::Engine::get().mix_rate;
}

int32_t FeluccaPlayback::_mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
	const felucca::Plan &p = felucca::Engine::get().plan;
	bool start = false, relocate = false;
	mixed += uint64_t(p_frames);
	if (hold_silent || hold_fade) {
		if (hold_fade && !hold_silent) {
			hold_fade--;
		}
		for (int32_t i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame{ 0.0f, 0.0f };
		}
		return p_frames;
	}
	// its place in the music, with a copy or without
	if (armed) {
		armed = false;
		started = start = true;
	} else if (started && std::fabs(p.b0 - last_b1) > 1e-9) {
		// the conductor's timeline moved while this playback was not mixed: its source was paused on its own. FOLLOW:
		// back where the music is now; HOLD: on from where it stopped, its origin later by the pause rounded to the
		// nearest of the source's beats or bars (on the grid still; QUANTIZE_NOW: by the pause exactly)
		if (join == 1) {
			const double gap = p.b0 - last_b1, unit = quantize == 2 ? 4.0 : quantize == 1 ? 1.0 : 0.0;
			origin += unit > 0.0 ? std::round(gap / unit) * unit : gap;
		}
		relocate = true;
	}
	last_b1 = p.b1;
	if (!core) {
		for (int32_t i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame{ 0.0f, 0.0f };
		}
		return p_frames;
	}
	if (p.tempo != tempo_applied) { // (before a locate: it places the tracks at the tempo it finds)
		core->tempo(p.tempo);
		tempo_applied = p.tempo;
	}
	if (start) {
		core->play(origin);
	} else if (started && (fresh || relocate)) {
		core->locate(origin); // (a copy given to music in progress joins it where it is)
	}
	render(p_buffer, p_frames, p.b0, p.b1);
	if (fresh) { // a short fade-in (the copy is silent until a step starts in it: this softens only one that starts here)
		fresh = false;
		for (int32_t i = 0; i < p_frames; i++) {
			const float g = float(i + 1) / float(p_frames);
			p_buffer[i] = AudioFrame{ p_buffer[i].left * g, p_buffer[i].right * g };
		}
	}
	return p_frames;
}

// p_frames output frames, the conductor's timeline moving from p_b0 to p_b1 over them
void FeluccaPlayback::render(AudioFrame *p_out, int32_t p_frames, double p_b0, double p_b1) {
	const double rate = felucca::Engine::get().mix_rate;
	if (rate == double(FEL_RATE)) { // the FM-1's own rate: frame for frame
		for (int32_t done = 0; done < p_frames;) {
			int32_t n = p_frames - done < CHUNK ? p_frames - done : CHUNK;
			double a = p_b0 + (p_b1 - p_b0) * done / p_frames, b = p_b0 + (p_b1 - p_b0) * (done + n) / p_frames;
			core->render(uint32_t(n), a, b, out_l.data(), out_r.data());
			for (int32_t i = 0; i < n; i++) {
				p_out[done + i] = AudioFrame{ out_l[size_t(i)], out_r[size_t(i)] };
			}
			done += n;
		}
		return;
	}
	// resampled: the instance's frames at 44.1 kHz into rs_l / rs_r, read between them (4-point Hermite) at
	// FEL_RATE / rate a frame. rs_pos - 1 .. rs_pos + 2 must be there; the conductor's
	// timeline maps onto the frames rendered this step
	const double inc = double(FEL_RATE) / rate;
	const int32_t max_out = int32_t(CHUNK / inc) - 4;
	for (int32_t done = 0; done < p_frames;) {
		int32_t n = p_frames - done < max_out ? p_frames - done : max_out;
		double a = p_b0 + (p_b1 - p_b0) * done / p_frames, b = p_b0 + (p_b1 - p_b0) * (done + n) / p_frames;
		int32_t need = int32_t(std::floor(rs_pos + (n - 1) * inc)) + 3;
		if (need > rs_have) {
			core->render(uint32_t(need - rs_have), a, b, rs_l.data() + rs_have, rs_r.data() + rs_have);
			rs_have = need;
		}
		for (int32_t i = 0; i < n; i++) {
			double x = rs_pos + i * inc;
			int32_t k = int32_t(std::floor(x));
			float f = float(x - k), o[2];
			const float *s[2] = { rs_l.data(), rs_r.data() };
			for (int c = 0; c < 2; c++) {
				float y0 = s[c][k - 1], y1 = s[c][k], y2 = s[c][k + 1], y3 = s[c][k + 2];
				float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
				float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
				o[c] = ((c3 * f + c2) * f + c1) * f + y1;
			}
			p_out[done + i] = AudioFrame{ o[0], o[1] };
		}
		rs_pos += n * inc;
		{ // keep one frame of history before the read position
			int32_t drop = int32_t(std::floor(rs_pos)) - 1;
			if (drop > 0) {
				for (int32_t i = drop; i < rs_have; i++) {
					rs_l[size_t(i - drop)] = rs_l[size_t(i)];
					rs_r[size_t(i - drop)] = rs_r[size_t(i)];
				}
				rs_have -= drop;
				rs_pos -= drop;
			}
		}
		done += n;
	}
}

Ref<FeluccaSnapshot> FeluccaPlayback::snapshot() const {
	Ref<FeluccaSnapshot> r;
	r.instantiate();
	r->slot = slot;
	if (!core) {
		return r;
	}
	fel_snapshot_t s;
	AudioServer::get_singleton()->lock();
	core->snapshot(&s);
	r->origin = origin;
	AudioServer::get_singleton()->unlock();
	r->bound = true;
	r->playing = s.playing != 0;
	r->beats = s.beats;
	r->frames = int64_t(s.frames);
	for (int t = 0; t < FEL_TRACKS; t++) {
		r->steps.push_back(int32_t(s.track[t].step));
		r->positions.push_back(int32_t(s.track[t].pos));
		r->loops.push_back(int32_t(s.track[t].loop));
	}
	return r;
}

void FeluccaSnapshot::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_bound"), &FeluccaSnapshot::is_bound);
	ClassDB::bind_method(D_METHOD("is_playing"), &FeluccaSnapshot::is_playing);
	ClassDB::bind_method(D_METHOD("get_beats"), &FeluccaSnapshot::get_beats);
	ClassDB::bind_method(D_METHOD("get_origin"), &FeluccaSnapshot::get_origin);
	ClassDB::bind_method(D_METHOD("get_frames"), &FeluccaSnapshot::get_frames);
	ClassDB::bind_method(D_METHOD("get_slot"), &FeluccaSnapshot::get_slot);
	ClassDB::bind_method(D_METHOD("get_steps"), &FeluccaSnapshot::get_steps);
	ClassDB::bind_method(D_METHOD("get_positions"), &FeluccaSnapshot::get_positions);
	ClassDB::bind_method(D_METHOD("get_loops"), &FeluccaSnapshot::get_loops);
	const uint32_t ro = PROPERTY_USAGE_NONE;
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bound", PROPERTY_HINT_NONE, "", ro), "", "is_bound");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "playing", PROPERTY_HINT_NONE, "", ro), "", "is_playing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "beats", PROPERTY_HINT_NONE, "", ro), "", "get_beats");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "origin", PROPERTY_HINT_NONE, "", ro), "", "get_origin");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frames", PROPERTY_HINT_NONE, "", ro), "", "get_frames");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "slot", PROPERTY_HINT_NONE, "", ro), "", "get_slot");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "steps", PROPERTY_HINT_NONE, "", ro), "", "get_steps");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "positions", PROPERTY_HINT_NONE, "", ro), "", "get_positions");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "loops", PROPERTY_HINT_NONE, "", ro), "", "get_loops");
}

} // namespace godot
