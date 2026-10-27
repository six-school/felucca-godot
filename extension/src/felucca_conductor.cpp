#include "felucca_conductor.h"

#include "felucca_clock.h"
#include "felucca_engine.h"
#include "felucca_schedule.h"
#include "felucca_source.h"
#include "felucca_stream.h"

#include <godot_cpp/classes/audio_listener2d.hpp>
#include <godot_cpp/classes/audio_listener3d.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstring>
#include <cmath>

namespace godot {

static const char *const CLOCK_BUS = "FeluccaClock";

FeluccaConductor *FeluccaConductor::singleton = nullptr;

FeluccaConductor::FeluccaConductor() {
	singleton = this;
}

FeluccaConductor::~FeluccaConductor() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void FeluccaConductor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tempo", "bpm"), &FeluccaConductor::set_tempo);
	ClassDB::bind_method(D_METHOD("get_tempo"), &FeluccaConductor::get_tempo);
	ClassDB::bind_method(D_METHOD("play"), &FeluccaConductor::play);
	ClassDB::bind_method(D_METHOD("stop"), &FeluccaConductor::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &FeluccaConductor::is_running);
	ClassDB::bind_method(D_METHOD("get_playhead"), &FeluccaConductor::get_playhead);
	ClassDB::bind_method(D_METHOD("get_bar"), &FeluccaConductor::get_bar);
	ClassDB::bind_method(D_METHOD("get_beat"), &FeluccaConductor::get_beat);
	ClassDB::bind_method(D_METHOD("get_pool_size"), &FeluccaConductor::get_pool_size);
	ClassDB::bind_method(D_METHOD("get_bound_count"), &FeluccaConductor::get_bound_count);
	ClassDB::bind_method(D_METHOD("boot_pool"), &FeluccaConductor::boot_pool);
	// (hidden: the tests' and debugging's; a leading _ keeps them out of the docs and completion)
	ClassDB::bind_method(D_METHOD("_get_rendered_playhead"), &FeluccaConductor::get_rendered_playhead);
	ClassDB::bind_method(D_METHOD("_get_booted_count"), &FeluccaConductor::get_booted_count);
	ClassDB::bind_method(D_METHOD("_get_mix_steps"), &FeluccaConductor::get_mix_steps);
	ClassDB::bind_method(D_METHOD("_get_paused_mix_steps"), &FeluccaConductor::get_paused_mix_steps);
	ClassDB::bind_method(D_METHOD("_get_mix_usec"), &FeluccaConductor::get_mix_usec);
	ClassDB::bind_method(D_METHOD("_get_boot_usec"), &FeluccaConductor::get_boot_usec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tempo", PROPERTY_HINT_RANGE, "1,999,0.001,or_greater"), "set_tempo", "get_tempo");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "running", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "is_running");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "playhead", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_playhead");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bar", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_bar");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "beat", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_beat");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pool_size", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_pool_size");
	// each whole beat of the playhead, when heard (DESIGN.md §5.2)
	ADD_SIGNAL(MethodInfo("on_beat", PropertyInfo(Variant::INT, "bar"), PropertyInfo(Variant::INT, "beat")));
}

// main thread: a change the audio thread sees from the next mix step, for every instance at once
#define PUBLISH(stmt)                              \
	do {                                           \
		AudioServer::get_singleton()->lock();      \
		stmt;                                      \
		felucca::Engine::get().republish();        \
		AudioServer::get_singleton()->unlock();    \
	} while (0)

void FeluccaConductor::set_tempo(double p_bpm) {
	if (!(p_bpm > 0.0) || !std::isfinite(p_bpm)) {
		return;
	}
	PUBLISH(felucca::Engine::get().tempo = p_bpm);
}

double FeluccaConductor::get_tempo() const {
	return felucca::Engine::get().tempo;
}

void FeluccaConductor::play() {
	ensure_node();
	PUBLISH(felucca::Engine::get().running = true);
}

void FeluccaConductor::stop() {
	PUBLISH(felucca::Engine::get().running = false);
}

bool FeluccaConductor::is_running() const {
	return felucca::Engine::get().running;
}

double FeluccaConductor::get_rendered_playhead() const {
	AudioServer::get_singleton()->lock();
	double b = felucca::Engine::get().plan.b0;
	AudioServer::get_singleton()->unlock();
	return b;
}

double FeluccaConductor::get_playhead() {
	const int64_t now = felucca::Engine::now_us();
	refresh_latency(now);
	AudioServer::get_singleton()->lock();
	const double h = heard_locked(now);
	AudioServer::get_singleton()->unlock();
	return h;
}

int FeluccaConductor::get_bar() {
	return int(std::floor(get_playhead() / 4.0)) + 1;
}

int FeluccaConductor::get_beat() {
	return int(int64_t(std::floor(get_playhead())) % 4) + 1;
}

// the DRUM engine's index (FeluccaPart.Engine's ENGINE_DRUM)
static int32_t drum_engine() {
	static int32_t d = -2;
	for (int k = 0; d == -2 && k < felucca::API_ENGINE_ENUM.count; k++) {
		d = String(felucca::API_ENGINE_ENUM.values[k].name) == "ENGINE_DRUM" ? felucca::API_ENGINE_ENUM.values[k].value : d;
	}
	return d;
}

void FeluccaConductor::refresh_latency(int64_t p_now) {
	if (p_now - latency_at > 1000000) {
		latency = AudioServer::get_singleton()->get_output_latency();
		latency_at = p_now;
	}
}

// the timeline as heard at p_now (DESIGN.md §6.3): the newest mix step whose start has been heard (its time in
// Engine::mixes plus the output latency), and the way into it; before any is, the oldest one's start. Never back (the
// latency figure is asked again now and then)
double FeluccaConductor::heard_locked(int64_t p_now) {
	felucca::Engine &e = felucca::Engine::get();
	const uint64_t k = e.steps.load(std::memory_order_relaxed);
	const int64_t lat = int64_t(latency * 1e6);
	double h = k ? heard_last : e.plan.b0;
	for (uint64_t j = k; j-- > 0 && k - j < felucca::Engine::MIXES - 1;) {
		const felucca::Engine::Mix &m = e.mixes[j % felucca::Engine::MIXES];
		if (m.step != j) {
			break;
		}
		h = m.b0;
		if (p_now >= m.at_us + lat) {
			const double f = std::min(double(p_now - m.at_us - lat) / (m.frames * 1e6 / e.mix_rate), 1.0);
			h = m.b0 + f * (m.b1 - m.b0);
			break;
		}
	}
	heard_last = std::max(heard_last, h);
	return heard_last;
}

// when the timeline's p_beats is heard: in the mix step whose stretch holds it (else p_now)
int64_t FeluccaConductor::heard_at_locked(double p_beats, int64_t p_now) {
	felucca::Engine &e = felucca::Engine::get();
	const uint64_t k = e.steps.load(std::memory_order_relaxed);
	for (uint64_t j = k; j-- > 0 && k - j < felucca::Engine::MIXES - 1;) {
		const felucca::Engine::Mix &m = e.mixes[j % felucca::Engine::MIXES];
		if (m.step != j || p_beats > m.b1) {
			break;
		}
		if (p_beats >= m.b0 && m.b1 > m.b0) {
			return m.at_us + int64_t((latency + (p_beats - m.b0) / (m.b1 - m.b0) * m.frames / e.mix_rate) * 1e6);
		}
	}
	return p_now;
}

int FeluccaConductor::get_pool_size() const {
	return felucca::Engine::get().size();
}

void FeluccaConductor::boot_pool() {
	felucca::Engine::get().boot_all();
}

int FeluccaConductor::get_bound_count() const {
	return felucca::Engine::get().bound();
}

int FeluccaConductor::get_booted_count() const {
	return felucca::Engine::get().booted();
}

int64_t FeluccaConductor::get_mix_steps() const {
	return int64_t(felucca::Engine::get().steps.load());
}

int64_t FeluccaConductor::get_paused_mix_steps() const {
	return int64_t(felucca::Engine::get().paused_steps.load());
}

// the time our playbacks' mixes have taken on the audio thread, added up
int64_t FeluccaConductor::get_mix_usec() const {
	return int64_t(felucca::Engine::get().mix_ns.load() / 1000);
}

// the time the pool's copies took to boot, added up
int64_t FeluccaConductor::get_boot_usec() const {
	return felucca::Engine::get().boot_us;
}

void FeluccaConductor::ensure_node() {
	if (node.is_valid() && ObjectDB::get_instance(node)) {
		return;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (!tree || !tree->get_root()) {
		return;
	}
	UtilityFunctions::push_warning("FeluccaConductor: no Felucca autoload (res://addons/felucca/felucca.tscn) in the project: ",
			"one is added after the scene, so the scene's sources hear a pause before it and the pause is not atomic");
	FeluccaConductorNode *n = memnew(FeluccaConductorNode);
	n->set_name("FeluccaConductor");
	node = n->get_instance_id();
	tree->get_root()->call_deferred("add_child", n);
}

void FeluccaConductor::adopt_node(Node *p_node) {
	node = p_node->get_instance_id();
}

void FeluccaConductor::add_playback(FeluccaPlayback *p_playback) {
	ensure_node();
	p_playback->seq = ++seq;
	live.push_back(p_playback);
	pending.push_back(p_playback);
}

void FeluccaConductor::remove_playback(FeluccaPlayback *p_playback) {
	live.erase(std::remove(live.begin(), live.end(), p_playback), live.end());
	pending.erase(std::remove(pending.begin(), pending.end(), p_playback), pending.end());
	for (size_t i = pend.size(); i-- > 0;) { // (its actions: cancelled with it, no signal)
		if (pend[i].p == p_playback) {
			forget(i);
		}
	}
	inbox.erase(std::remove_if(inbox.begin(), inbox.end(), [&](const Inbound &in) { return in.p == p_playback; }), inbox.end());
}

// every play() of this frame starts at one origin: the conductor's timeline where the next mix step starts, rounded up
// to the source's beat or bar (4 beats from the timeline's 0) when it asks
void FeluccaConductor::resolve_pending() {
	if (pending.empty()) {
		return;
	}
	AudioServer::get_singleton()->lock();
	double b = felucca::Engine::get().plan.b0;
	for (FeluccaPlayback *p : pending) {
		double unit = p->quantize == FeluccaSource::QUANTIZE_BAR ? 4.0 : p->quantize == FeluccaSource::QUANTIZE_BEAT ? 1.0 : 0.0;
		p->origin = unit > 0.0 ? std::ceil(b / unit - 1e-9) * unit : b;
		p->armed = true;
		p->timed = true;
	}
	AudioServer::get_singleton()->unlock();
	pending.clear();
}

// where p_viewport hears from this frame, as Godot's players reckon it (AudioStreamPlayer2D / 3D::_update_panning,
// 4.7.2): in 2D its current AudioListener2D, else the screen's centre in the canvas; in 3D its current AudioListener3D,
// else its camera. Looked up once a viewport a pass
const FeluccaConductor::Ear *FeluccaConductor::ear(Viewport *p_viewport) {
	if (!p_viewport) {
		return nullptr;
	}
	for (const Ear &e : ears) {
		if (e.vp == p_viewport) {
			return &e;
		}
	}
	Ear e;
	e.vp = p_viewport;
	if (p_viewport->is_audio_listener_2d()) {
		e.hears_2d = true;
		if (AudioListener2D *l = p_viewport->get_audio_listener_2d()) {
			e.at_2d = l->get_global_position();
		} else {
			const Transform2D t = p_viewport->get_global_canvas_transform() * p_viewport->get_canvas_transform();
			e.at_2d = t.affine_inverse().xform(p_viewport->get_visible_rect().size * 0.5);
		}
	}
	if (p_viewport->is_audio_listener_3d()) {
		Node3D *l = p_viewport->get_audio_listener_3d();
		if (!l) {
			l = p_viewport->get_camera_3d();
		}
		if (l) {
			e.hears_3d = true;
			e.at_3d = l->get_global_position();
		}
	}
	ears.push_back(e);
	return &ears.back();
}

// how far into its range a source is (DESIGN.md §7): its distance to the listener over its max_distance, in range up
// to 1 (Godot mixes it at all: its gain reaches 0 at max_distance). 0 always for a non-positional source, and for a 3D
// one without a max_distance (no limit); infinite when Godot does not mix it (no listener, or paused by its own
// stream_paused: r_paused). Godot takes the loudest over every listening viewport of the world (split screens); this
// is the source's own
double FeluccaConductor::reach(Node *p_node, bool &r_paused) {
	r_paused = false;
	if (!p_node || !p_node->is_inside_tree()) {
		return INFINITY;
	}
	if (AudioStreamPlayer2D *s = Object::cast_to<AudioStreamPlayer2D>(p_node)) {
		const Ear *e = ear(s->get_viewport());
		if ((r_paused = s->get_stream_paused()) || !e || !e->hears_2d) {
			return INFINITY;
		}
		const double max = s->get_max_distance(), d = s->get_global_position().distance_to(e->at_2d);
		return max > 0.0 ? d / max : d > 0.0 ? INFINITY : 0.0;
	}
	if (AudioStreamPlayer3D *s = Object::cast_to<AudioStreamPlayer3D>(p_node)) {
		const Ear *e = ear(s->get_viewport());
		if ((r_paused = s->get_stream_paused()) || !e || !e->hears_3d) {
			return INFINITY;
		}
		const double max = s->get_max_distance();
		return max > 0.0 ? s->get_global_position().distance_to(e->at_3d) / max : 0.0;
	}
	if (AudioStreamPlayer *s = Object::cast_to<AudioStreamPlayer>(p_node)) {
		return (r_paused = s->get_stream_paused()) ? INFINITY : 0.0;
	}
	return INFINITY;
}

// the binding pass (DESIGN.md §7): copies to the sources pinned (actions pending, or pinned: whatever their range,
// never evicted; more than copies: the farthest wait, a warning once), then to the sources in range of the listener,
// the nearest first, and to the others while some are free (lazy: a source walking into range is bound already). A source in range with none free
// takes the copy of the bound source farthest out of range; with every bound source in range (more sources in range
// than copies: the level-design rule broken, a warning once) the farthest one's, if it is nearer. An evicted copy fades
// out over one mix step (FeluccaPlayback::leave: after a teleport Godot may still mix a source left behind at its old
// gain until its next physics tick) and is handed over on a later pass; a paused one, which Godot does not mix, at once
void FeluccaConductor::binding_pass() {
	felucca::Engine &e = felucca::Engine::get();
	ears.clear();
	waiting.clear();
	holders.clear();
	// copies on their way back: fading out (each one for a source waiting in range), or their source stopped (free
	// once Godot has destroyed its playback, a frame or so later)
	int coming = 0;
	for (FeluccaPlayback *p : live) {
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(p->source));
		const FelSource *f = n ? FelSource::of(n) : nullptr;
		const bool pinned = p->pins > 0 || (f && f->pinned);
		bool paused = false;
		const double r = reach(n, paused);
		if (p->leaving) {
			if (!p->left.load(std::memory_order_acquire) && !paused) {
				coming++;
				continue;
			}
			p->unbind(); // (faded out: cut and free)
		}
		if (p->is_bound()) {
			if (!p->is_active()) {
				coming++;
			} else if (!pinned) { // (a pinned one keeps its copy)
				holders.push_back({ p, r, paused, false });
			}
		} else if (p->wants_copy()) {
			waiting.push_back({ p, r, paused, pinned });
		}
	}
	const auto nearer = [](const Rank &a, const Rank &b) {
		return a.reach < b.reach || (a.reach == b.reach && a.p->seq < b.p->seq);
	};
	std::sort(waiting.begin(), waiting.end(), [&](const Rank &a, const Rank &b) {
		return a.pinned != b.pinned ? a.pinned : nearer(a, b); // (the pinned first)
	});
	std::sort(holders.begin(), holders.end(), [&](const Rank &a, const Rank &b) { return nearer(b, a); });
	size_t next = 0; // holders[next]: the farthest bound source not evicted by this pass
	for (const Rank &w : waiting) {
		const int slot = e.acquire();
		if (slot >= 0) {
			w.p->bind(slot);
			continue;
		}
		if (!w.pinned && w.reach > 1.0) {
			break; // (out of range: it never takes a copy, nor do the ones after it)
		}
		if (coming > 0) {
			coming--;
			continue;
		}
		if (next == holders.size() && w.pinned) { // (every copy with a pinned source: the farthest pinned wait)
			if (!pinned_warned) {
				pinned_warned = true;
				UtilityFunctions::push_warning("FeluccaConductor: more pinned Felucca sources than engines available (pool size: ",
						e.size(), ").");
			}
			break;
		}
		if (next == holders.size() || holders[next].reach <= 1.0) { // (a pinned source counts as one in range)
			if (!overflow_warned) {
				overflow_warned = true;
				UtilityFunctions::push_warning("FeluccaConductor: more Felucca sources in range than engines available (pool size: ",
						e.size(), ").");
			}
			if (next == holders.size() || (!w.pinned && !(w.reach < holders[next].reach))) {
				break; // (not nearer than the farthest: it waits, and so do the ones after it)
			}
		}
		const Rank &h = holders[next++];
		if (h.paused) {
			h.p->unbind();
			if (const int s = e.acquire(); s >= 0) {
				w.p->bind(s);
			}
		} else {
			h.p->leave();
		}
	}
}

// ---------------------------------------------------------------- scheduling (M6, DESIGN.md §5.2, §6.3) ---

Ref<FeluccaSchedule> FeluccaConductor::schedule(Node *p_source, FeluccaPlayback *p_playback, const Ref<FeluccaWhen> &p_when,
		const Ref<FeluccaAction> &p_action, const StringName &p_tag, const Callable &p_callback) {
	const FelSource *f = FelSource::of(p_source);
	if (p_when.is_null() || p_action.is_null()) {
		UtilityFunctions::push_error(p_source->get_name(), ": schedule() needs a FeluccaWhen and a FeluccaAction.");
		return Ref<FeluccaSchedule>();
	}
	if ((p_when->values && p_when->values != f->values) || (p_action->values && p_action->values != f->values)) {
		UtilityFunctions::push_error(p_source->get_name(), ": schedule() with a part of another source.");
		return Ref<FeluccaSchedule>();
	}
	if (p_playback->pins >= FEL_SCHEDULES) {
		UtilityFunctions::push_error(p_source->get_name(), ": schedule(): ", FEL_SCHEDULES, " actions pending already.");
		return Ref<FeluccaSchedule>();
	}
	Pending x;
	x.id = ++next_id ? next_id : ++next_id; // (0 is none)
	x.p = p_playback;
	x.source = ObjectID(p_source->get_instance_id());
	x.tag = p_tag;
	x.action = p_action;
	x.callback = p_callback;
	x.handle.instantiate();
	x.handle->id = x.id;
	x.handle->tag = p_tag;
	if (p_when->bar) { // the conductor's next bar (4 beats from its 0, as QUANTIZE_BAR's), on: the bar after
		AudioServer::get_singleton()->lock();
		const double b = felucca::Engine::get().plan.b0;
		AudioServer::get_singleton()->unlock();
		x.when = { FEL_AT_BEATS, 0, uint8_t(p_when->late), 0, 0, std::floor(b / 4.0) * 4.0 + 4.0 + p_when->beat, 4.0 };
		x.part = p_action->action.kind == FEL_DO_MARK ? 0 : p_action->action.track + 1;
	} else {
		x.when = { FEL_AT_STEP, uint8_t(p_when->track), uint8_t(p_when->late), uint16_t(p_when->step),
			uint32_t(p_when->loops), 0.0, 0.0 };
		x.part = p_when->track + 1;
	}
	pend.push_back(x);
	p_playback->pins++;
	if (p_playback->is_bound()) {
		AudioServer::get_singleton()->lock();
		p_playback->core->schedule(&x.when, &p_action->action, x.id);
		AudioServer::get_singleton()->unlock();
	}
	return x.handle;
}

// a playback binding: its actions into the instance, before it is mixed (no lock), resolved there once it plays
void FeluccaConductor::send_pending(FeluccaPlayback *p_playback, const fel_core_t *p_core) {
	for (const Pending &x : pend) {
		if (x.p == p_playback && !x.applied) {
			p_core->schedule(&x.when, &x.action->action, x.id);
		}
	}
}

void FeluccaConductor::forget(size_t p_index) {
	Pending &x = pend[p_index];
	x.p->pins--;
	x.handle->pending = false;
	pend.erase(pend.begin() + std::ptrdiff_t(p_index));
}

void FeluccaConductor::cancel_tag(Node *p_source, const StringName &p_tag) {
	const ObjectID id(p_source->get_instance_id());
	AudioServer::get_singleton()->lock();
	for (size_t i = pend.size(); i-- > 0;) {
		if (pend[i].source == id && pend[i].tag == p_tag) {
			if (pend[i].p->is_bound()) {
				pend[i].p->core->cancel(pend[i].id);
			}
			forget(i);
		}
	}
	AudioServer::get_singleton()->unlock();
}

void FeluccaConductor::cancel_id(uint32_t p_id) {
	for (size_t i = 0; i < pend.size(); i++) {
		if (pend[i].id == p_id) {
			if (pend[i].p->is_bound()) {
				AudioServer::get_singleton()->lock();
				pend[i].p->core->cancel(p_id);
				AudioServer::get_singleton()->unlock();
			}
			forget(i);
			return;
		}
	}
}

void FeluccaConductor::forget_all() {
	while (!pend.empty()) {
		forget(pend.size() - 1);
	}
	inbox.clear();
}

// a source's music as heard (FeluccaSource.get_snapshot): the playhead as heard, less its origin; each part's step as
// its start was heard (the steps taken by the last deliver, and any of those still ahead heard since), its way into
// it by the playhead against the step's start and length (exact unswung; swung, the step's own length)
Ref<FeluccaSnapshot> FeluccaConductor::heard_snapshot(FeluccaPlayback *p_playback) {
	Ref<FeluccaSnapshot> r;
	r.instantiate();
	const int64_t now = felucca::Engine::now_us();
	refresh_latency(now);
	AudioServer::get_singleton()->lock();
	const double h = heard_locked(now), origin = p_playback->origin;
	AudioServer::get_singleton()->unlock();
	r->origin = origin;
	r->playhead = p_playback->timed ? h - origin : 0.0;
	r->bound = p_playback->is_bound();
	if (!r->bound) {
		return r;
	}
	FeluccaPlayback::PartHeard parts[FEL_TRACKS];
	for (int t = 0; t < FEL_TRACKS; t++) {
		parts[t] = p_playback->heard_parts[t];
	}
	for (const FeluccaPlayback::Ahead &a : p_playback->ahead) {
		if (a.kind == FEL_EV_STEP && a.heard_us <= now) {
			parts[a.track] = { a.step, a.loop, a.start, a.length };
		}
	}
	for (int t = 0; t < FEL_TRACKS; t++) {
		const FeluccaPlayback::PartHeard &q = parts[t];
		r->playing |= q.step > 0;
		r->steps.push_back(q.step);
		r->loops.push_back(q.loop);
		r->progress.push_back(q.step > 0 && q.length > 0.0 ? float(CLAMP((h - q.start) / q.length, 0.0, 0.9999)) : 0.0f);
	}
	return r;
}

// once a frame, after the binding pass: bound / unbound; the actions of sources stopped (or played again) cancelled;
// the playbacks' events taken (under the lock, once) and mapped to when they are heard: a mix step's start is heard
// at its time (Engine::mixes) plus the output latency, an event in it by its place on the step's stretch of the
// conductor's timeline; then what has been heard by now, in that order (at one moment: the conductor's beat, a part's
// loop, its step, its notes as the instance played them, an action): on_beat, a part's on_loop / on_step / on_note /
// on_hit / on_noteoff (only while something listens: a part's notes no one listens to are dropped as they are taken),
// an action's callback, its handle's triggered and its source's schedule_triggered (a dropped action: at once)
void FeluccaConductor::deliver() {
	felucca::Engine &e = felucca::Engine::get();
	const int64_t now = felucca::Engine::now_us();
	if (!touched.empty()) {
		std::vector<ObjectID> t;
		t.swap(touched); // (a handler may touch more: the next frame's)
		std::sort(t.begin(), t.end(), [](ObjectID a, ObjectID b) { return uint64_t(a) < uint64_t(b); });
		t.erase(std::unique(t.begin(), t.end()), t.end());
		for (ObjectID id : t) {
			Node *n = Object::cast_to<Node>(ObjectDB::get_instance(id));
			FelSource *f = n ? FelSource::of(n) : nullptr;
			if (f && f->bound(n) != f->reported) {
				f->reported = !f->reported;
				n->emit_signal(f->reported ? "bound" : "unbound");
			}
		}
	}
	{
		std::vector<std::pair<ObjectID, FeluccaPlayback *>> playing; // (a source's playback while it plays, once)
		bool locked = false;
		for (size_t i = pend.size(); i-- > 0;) {
			Pending &x = pend[i];
			auto it = std::find_if(playing.begin(), playing.end(), [&](const auto &q) { return q.first == x.source; });
			if (it == playing.end()) {
				Node *n = Object::cast_to<Node>(ObjectDB::get_instance(x.source));
				playing.push_back({ x.source, n ? FelSource::playing(n) : nullptr });
				it = playing.end() - 1;
			}
			if (it->second == x.p) {
				continue;
			}
			if (x.p->is_bound()) {
				if (!locked) {
					AudioServer::get_singleton()->lock();
					locked = true;
				}
				x.p->core->cancel(x.id);
			}
			forget(i);
		}
		if (locked) {
			AudioServer::get_singleton()->unlock();
		}
	}
	enum { BEAT, LOOP, STEP, NOTE, ACTION };
	struct Emit {
		int kind;
		int64_t at;
		ObjectID source; // (LOOP, STEP, NOTE: the part's source)
		int a = 0, b = 0, c = 0, d = -1; // BEAT: bar, beat; LOOP, STEP: track, step, loop (1-based); NOTE: track,
										 // note, velocity, lane
		Pending x = {}; // ACTION
		bool dropped = false, off = false; // (NOTE: a release; its order with the notes started kept as played)
	};
	std::vector<Emit> out;
	listening.clear();
	for (FeluccaPlayback *p : live) { // the parts' notes listened to (an on_hit on a part not DRUM: said once)
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(p->source));
		const FelSource *f = n ? FelSource::of(n) : nullptr;
		uint32_t l = 0;
		for (int t = 0; f && t < FEL_TRACKS; t++) {
			const Ref<FeluccaPart> &q = f->parts[t];
			if (q.is_null()) {
				continue;
			}
			const bool hits = q->has_connections("on_hit");
			l |= (hits || q->has_connections("on_note") || q->has_connections("on_noteoff") ? 1u : 0u) << t;
			if (hits && !hit_warned && q->get_engine() != drum_engine()) {
				hit_warned = true;
				UtilityFunctions::push_warning(n->get_name(), ": on_hit is connected on part ", t + 1,
						", which is not a DRUM part, so it will not fire.");
			}
		}
		listening.push_back(l);
	}
	refresh_latency(now);
	AudioServer::get_singleton()->lock();
	for (size_t i = 0; i < live.size(); i++) {
		FeluccaPlayback *p = live[i];
		for (int t = 0; t < FEL_TRACKS; t++) { // (no longer listened to: no release to pair any more)
			if (!((listening[i] >> t) & 1u)) {
				memset(p->notes_on[t], 0, sizeof p->notes_on[t]);
			}
		}
		p->listening = listening[i];
		taken.clear();
		p->take_events(taken);
		for (const FelHeard &h : taken) {
			inbox.push_back({ p, h.e, h.step, -1 });
		}
	}
	for (Inbound &in : inbox) {
		if (in.heard_us >= 0) {
			continue;
		}
		const felucca::Engine::Mix &m = e.mixes[in.step % felucca::Engine::MIXES];
		if (m.step == UINT64_MAX || m.step < in.step) {
			continue; // (its mix step not over yet: the next frame)
		}
		if (m.step > in.step) { // (long gone: the record was written over)
			in.heard_us = now;
			continue;
		}
		const double at = m.b1 > m.b0 ? (in.e.beats - m.b0) / (m.b1 - m.b0) : 0.0;
		in.heard_us = m.at_us + int64_t((latency + at * m.frames / e.mix_rate) * 1e6);
	}
	{ // the conductor's whole beats heard since (the first: once the playhead has moved off 0)
		const double h = heard_locked(now);
		const int64_t upto = h > 0.0 ? int64_t(std::floor(h)) : -1;
		for (int64_t b = last_beat + 1; b <= upto; b++) {
			Emit x = { BEAT, heard_at_locked(double(b), now), ObjectID() };
			x.a = int(b / 4) + 1;
			x.b = int(b % 4) + 1;
			out.push_back(x);
		}
		last_beat = std::max(last_beat, upto);
	}
	AudioServer::get_singleton()->unlock();

	for (size_t i = 0; i < inbox.size();) {
		const Inbound in = inbox[i];
		if (in.heard_us < 0) {
			i++;
			continue;
		}
		inbox.erase(inbox.begin() + std::ptrdiff_t(i));
		if (in.e.kind == FEL_EV_NOTE || in.e.kind == FEL_EV_NOTE_OFF) { // (a release only of a note taken as on)
			uint32_t &on = in.p->notes_on[in.e.track][in.e.note >> 5];
			const uint32_t bit = 1u << (in.e.note & 31u);
			const bool was = on & bit;
			on = in.e.kind == FEL_EV_NOTE ? on | bit : on & ~bit;
			if (in.e.kind == FEL_EV_NOTE_OFF && !was) {
				continue;
			}
		}
		if (in.e.kind == FEL_EV_STEP || in.e.kind == FEL_EV_NOTE || in.e.kind == FEL_EV_NOTE_OFF) { // (to be heard: the
																										   // snapshot's, the parts' signals)
			in.p->ahead.push_back({ in.e.kind, in.e.track, in.e.step + 1, int32_t(in.e.loop) + 1, in.e.beats, in.e.length,
					in.e.note, in.e.velocity, in.e.lane, in.heard_us });
			continue;
		}
		auto it = std::find_if(pend.begin(), pend.end(), [&](const Pending &x) { return x.id == in.e.id; });
		if (it == pend.end()) {
			continue; // (cancelled meanwhile)
		}
		it->step = in.e.step + 1;
		it->loop = int(in.e.loop) + 1;
		it->beats = in.e.beats;
		if (in.e.kind == FEL_EV_DROPPED) {
			Emit x = { ACTION, now, it->source };
			x.x = *it;
			x.dropped = true;
			out.push_back(x);
			forget(size_t(it - pend.begin()));
			continue;
		}
		it->applied = true;
		it->heard_us = in.heard_us;
		const fel_action_t &a = it->action->action;
		if (a.kind == FEL_DO_SET && it->action->values) { // (kept: a binding after plays it)
			it->action->values->applied_track(a.track, a.a, a.b);
		}
	}
	for (FeluccaPlayback *p : live) { // the steps and notes heard by now
		size_t n = 0;
		for (; n < p->ahead.size() && p->ahead[n].heard_us <= now; n++) {
			const FeluccaPlayback::Ahead &a = p->ahead[n];
			if (a.kind != FEL_EV_STEP) {
				Emit x = { NOTE, a.heard_us, p->source };
				x.a = a.track;
				x.b = a.note;
				x.c = a.velocity;
				x.d = a.lane;
				x.off = a.kind == FEL_EV_NOTE_OFF;
				out.push_back(x);
				continue;
			}
			p->heard_parts[a.track] = { a.step, a.loop, a.start, a.length };
			if (a.step == 1) {
				Emit x = { LOOP, a.heard_us, p->source };
				x.a = a.track;
				x.c = a.loop;
				out.push_back(x);
			}
			Emit x = { STEP, a.heard_us, p->source };
			x.a = a.track;
			x.b = a.step;
			x.c = a.loop;
			out.push_back(x);
		}
		p->ahead.erase(p->ahead.begin(), p->ahead.begin() + std::ptrdiff_t(n));
	}
	for (size_t i = pend.size(); i-- > 0;) {
		if (pend[i].applied && pend[i].heard_us <= now) {
			Emit x = { ACTION, pend[i].heard_us, pend[i].source };
			x.x = pend[i];
			out.push_back(x);
			forget(i);
		}
	}
	std::stable_sort(out.begin(), out.end(), [](const Emit &a, const Emit &b) {
		return a.at != b.at ? a.at < b.at : a.kind < b.kind;
	});
	for (const Emit &x : out) {
		if (x.kind == BEAT) {
			emit_signal("on_beat", x.a, x.b);
			continue;
		}
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(x.source));
		if (x.kind != ACTION) {
			FelSource *f = n ? FelSource::of(n) : nullptr;
			const Ref<FeluccaPart> q = f ? f->parts[x.a] : Ref<FeluccaPart>();
			if (q.is_null()) {
				continue;
			}
			if (x.kind == LOOP && q->has_connections("on_loop")) {
				q->emit_signal("on_loop", x.c);
			} else if (x.kind == STEP && q->has_connections("on_step")) {
				q->emit_signal("on_step", x.b, x.c);
			} else if (x.kind == NOTE && x.off) {
				q->emit_signal("on_noteoff", x.b);
			} else if (x.kind == NOTE) {
				q->emit_signal("on_note", x.b, x.c);
				if (x.d >= 0) {
					q->emit_signal("on_hit", x.d, x.c);
				}
			}
			continue;
		}
		Ref<FeluccaEvent> ev;
		ev.instantiate();
		ev->tag = x.x.tag;
		ev->action = x.x.action;
		ev->part = x.x.part;
		ev->step = x.x.step;
		ev->loop = x.x.loop;
		ev->playhead = x.x.beats;
		ev->dropped = x.dropped;
		ev->late_by = x.dropped ? 0.0 : double(now - x.x.heard_us) / 1e6;
		if (x.x.callback.is_valid()) {
			x.x.callback.call(ev);
		}
		x.x.handle->emit_signal("triggered", ev);
		if (n) {
			n->emit_signal("schedule_triggered", x.x.tag, ev);
		}
	}
}

// D7: one lock for the conductor's timeline and every bound source, whatever its process mode (a source left playing
// would only ring on, its music standing with the timeline; DESIGN.md §13.13: its own time while the timeline stands is
// for later). A source its own stream_paused had paused already is noted and left paused; one Godot will unpause (it is
// told the pause: it cannot process while the tree is paused) is kept so through that (godot #65743) by its own
// UNPAUSED (FeluccaSource)
void FeluccaConductor::tree_paused(bool p_paused) {
	const int64_t now = felucca::Engine::now_us();
	if (p_paused) {
		paused_at = now;
	} else { // (what was not heard yet when the pause began is heard as much later)
		for (Pending &x : pend) {
			x.heard_us += x.applied && x.heard_us > paused_at ? now - paused_at : 0;
		}
		for (Inbound &in : inbox) {
			in.heard_us += in.heard_us > paused_at ? now - paused_at : 0;
		}
		for (FeluccaPlayback *p : live) {
			for (FeluccaPlayback::Ahead &a : p->ahead) {
				a.heard_us += a.heard_us > paused_at ? now - paused_at : 0;
			}
		}
	}
	AudioServer::get_singleton()->lock();
	felucca::Engine::get().paused = p_paused;
	felucca::Engine::get().republish();
	for (FeluccaPlayback *p : live) {
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(p->source));
		if (!n || !n->is_inside_tree()) {
			continue;
		}
		if (p_paused) {
			p->tree_held = true;
			p->tree_told = !n->can_process();
			p->user_paused = bool(n->call("get_stream_paused"));
			if (!p->user_paused) {
				n->call("set_stream_paused", true);
			}
		} else if (p->tree_held) {
			p->tree_held = false;
			if (!p->user_paused) {
				n->call("set_stream_paused", false);
			} else if (p->tree_told && FeluccaSource::is_source(n)) {
				p->hold_silent = true; // (until its UNPAUSED, right after Godot's, pauses it again)
			} else {
				p->user_paused = false; // (Godot leaves it as it is: paused)
			}
		}
	}
	AudioServer::get_singleton()->unlock();
}

void FeluccaConductorNode::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (FeluccaConductor *c = FeluccaConductor::get_singleton()) {
				c->adopt_node(this);
			}
			if (!Engine::get_singleton()->is_editor_hint()) {
				felucca::Engine::get().boot_all(); // (the game's start: no copy boots later, mid-game)
			}
			AudioServer *as = AudioServer::get_singleton();
			felucca::Engine &e = felucca::Engine::get();
			as->lock();
			e.mix_rate = as->get_mix_rate();
			as->unlock();
			if (as->get_bus_index(CLOCK_BUS) < 0) {
				as->add_bus();
				int b = as->get_bus_count() - 1;
				as->set_bus_name(b, CLOCK_BUS);
				as->set_bus_mute(b, true);
				Ref<FeluccaClockEffect> fx;
				fx.instantiate();
				as->add_bus_effect(b, fx);
			}
			set_process_priority(1 << 30); // (last in each frame's process: that frame's play()s are all in)
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS:
			if (FeluccaConductor *c = FeluccaConductor::get_singleton()) {
				c->resolve_pending();
				c->binding_pass();
				c->deliver();
			}
			break;
		case NOTIFICATION_PAUSED:
		case NOTIFICATION_UNPAUSED:
			if (FeluccaConductor *c = FeluccaConductor::get_singleton()) {
				c->tree_paused(p_what == NOTIFICATION_PAUSED);
			}
			break;
		case NOTIFICATION_EXIT_TREE: {
			if (FeluccaConductor *c = FeluccaConductor::get_singleton()) {
				c->forget_all();
			}
			AudioServer *as = AudioServer::get_singleton();
			int b = as->get_bus_index(CLOCK_BUS);
			if (b >= 0) {
				as->remove_bus(b);
			}
		} break;
	}
}

} // namespace godot
