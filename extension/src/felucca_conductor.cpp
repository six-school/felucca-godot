#include "felucca_conductor.h"

#include "felucca_clock.h"
#include "felucca_engine.h"
#include "felucca_source.h"
#include "felucca_stream.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
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

// D7: one lock for the conductor's timeline and every bound source, whatever its process mode (a source left playing
// would only ring on, its music standing with the timeline; DESIGN.md §13.17: its own time while the timeline stands is
// for M5 / M6). A source its own stream_paused had paused already is noted and left paused; one Godot will unpause (it
// is told the pause: it cannot process while the tree is paused) is kept so through that (godot #65743) by its own
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
