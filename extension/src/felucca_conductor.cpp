#include "felucca_conductor.h"

#include "felucca_clock.h"
#include "felucca_engine.h"
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
	ClassDB::bind_method(D_METHOD("get_beats"), &FeluccaConductor::get_beats);
	ClassDB::bind_method(D_METHOD("get_pool_size"), &FeluccaConductor::get_pool_size);
	ClassDB::bind_method(D_METHOD("get_bound_count"), &FeluccaConductor::get_bound_count);
	ClassDB::bind_method(D_METHOD("get_booted_count"), &FeluccaConductor::get_booted_count);
	ClassDB::bind_method(D_METHOD("boot_pool"), &FeluccaConductor::boot_pool);
	ClassDB::bind_method(D_METHOD("get_mix_steps"), &FeluccaConductor::get_mix_steps);
	ClassDB::bind_method(D_METHOD("get_paused_mix_steps"), &FeluccaConductor::get_paused_mix_steps);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tempo", PROPERTY_HINT_RANGE, "1,999,0.001,or_greater"), "set_tempo", "get_tempo");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "running", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "is_running");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "beats", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_beats");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pool_size", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_pool_size");
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

double FeluccaConductor::get_beats() const {
	AudioServer::get_singleton()->lock();
	double b = felucca::Engine::get().plan.b0;
	AudioServer::get_singleton()->unlock();
	return b;
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

// the binding pass (DESIGN.md §7): copies to the sources in range of the listener, the nearest first, and to the
// others while some are free (lazy: a source walking into range is bound already). A source in range with none free
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
		bool paused = false;
		const double r = reach(Object::cast_to<Node>(ObjectDB::get_instance(p->source)), paused);
		if (p->leaving) {
			if (!p->left.load(std::memory_order_acquire) && !paused) {
				coming++;
				continue;
			}
			p->unbind(); // (faded out: cut and free)
		}
		if (p->is_bound()) {
			if (p->is_active()) {
				holders.push_back({ p, r, paused });
			} else {
				coming++;
			}
		} else if (p->wants_copy()) {
			waiting.push_back({ p, r, paused });
		}
	}
	const auto nearer = [](const Rank &a, const Rank &b) {
		return a.reach < b.reach || (a.reach == b.reach && a.p->seq < b.p->seq);
	};
	std::sort(waiting.begin(), waiting.end(), nearer);
	std::sort(holders.begin(), holders.end(), [&](const Rank &a, const Rank &b) { return nearer(b, a); });
	size_t next = 0; // holders[next]: the farthest bound source not evicted by this pass
	for (const Rank &w : waiting) {
		const int slot = e.acquire();
		if (slot >= 0) {
			w.p->bind(slot);
			continue;
		}
		if (w.reach > 1.0) {
			break; // (out of range: it never takes a copy, nor do the ones after it)
		}
		if (coming > 0) {
			coming--;
			continue;
		}
		if (next == holders.size() || holders[next].reach <= 1.0) {
			if (!overflow_warned) {
				overflow_warned = true;
				UtilityFunctions::push_warning("FeluccaConductor: more Felucca sources in range than engines available (pool size: ",
						e.size(), ").");
			}
			if (next == holders.size() || !(w.reach < holders[next].reach)) {
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

// D7: one lock for the conductor's timeline and every bound source, whatever its process mode (a source left playing
// would only ring on, its music standing with the timeline; DESIGN.md §13.13: its own time while the timeline stands is
// for later). A source its own stream_paused had paused already is noted and left paused; one Godot will unpause (it is
// told the pause: it cannot process while the tree is paused) is kept so through that (godot #65743) by its own
// UNPAUSED (FeluccaSource)
void FeluccaConductor::tree_paused(bool p_paused) {
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
			}
			break;
		case NOTIFICATION_PAUSED:
		case NOTIFICATION_UNPAUSED:
			if (FeluccaConductor *c = FeluccaConductor::get_singleton()) {
				c->tree_paused(p_what == NOTIFICATION_PAUSED);
			}
			break;
		case NOTIFICATION_EXIT_TREE: {
			AudioServer *as = AudioServer::get_singleton();
			int b = as->get_bus_index(CLOCK_BUS);
			if (b >= 0) {
				as->remove_bus(b);
			}
		} break;
	}
}

} // namespace godot
