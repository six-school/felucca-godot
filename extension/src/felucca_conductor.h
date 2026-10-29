// The conductor: FeluccaConductor, an engine singleton as Input is (FeluccaConductor.tempo = 140.0),
// owns the timeline, its tempo and transport and the pool's playbacks. Its node, FeluccaConductorNode, is the project's
// autoload (addons/felucca/felucca.tscn): Godot tells a pause to the root's children in the order they were added
// (Node::_propagate_pause_notification walks data.children, not the child index), and autoloads are added before the
// main scene, so it hears the tree's PAUSED / UNPAUSED before any scene node: it stops the timeline and pauses every
// bound source's player in one AudioServer::lock(), and Godot's own per-node pauses after it change nothing. It also
// owns the clock effect's muted bus, and resolves each frame's play()s to one origin. Without the autoload one is added
// at run time, after the scene: then the pause is not atomic (a warning says so).
#pragma once

#include "fel_core.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

namespace godot {

class FeluccaPlayback;
class Viewport;
class FeluccaWhen;
class FeluccaAction;
class FeluccaSchedule;
class FeluccaSnapshot;
struct FelHeard;

class FeluccaConductor : public Object {
	GDCLASS(FeluccaConductor, Object);
	static FeluccaConductor *singleton;
	ObjectID node;
	std::vector<FeluccaPlayback *> live, pending; // main thread (live: in the order of their play()s)
	uint64_t seq = 0;
	// the binding pass's (main thread, kept from frame to frame: nothing allocated once they have grown)
	struct Ear { // where a viewport hears from, this frame (Godot's own choice: reach())
		Viewport *vp = nullptr;
		bool hears_2d = false, hears_3d = false;
		Vector2 at_2d;
		Vector3 at_3d;
	};
	struct Rank {
		FeluccaPlayback *p;
		double reach;
		bool paused; // (Godot does not mix it)
		bool pinned; // (actions pending, or pinned: it always has an instance)
	};
	std::vector<Ear> ears;
	std::vector<Rank> waiting, holders;
	bool overflow_warned = false, pinned_warned = false;

	// scheduling: the actions pending on the sources playing, until heard (or dropped, cancelled)
	struct Pending {
		uint32_t id;
		FeluccaPlayback *p; // the playback it was scheduled on (it goes with it: stopped, freed)
		ObjectID source;
		StringName tag;
		Ref<FeluccaAction> action;
		Ref<FeluccaSchedule> handle;
		Callable callback; // (none: invalid)
		fel_when_t when; // as the instance takes it (next_bar's beats resolved when scheduled)
		int part; // the event's (1 .. 4, 0 none)
		bool applied = false; // in the instance: its event is on its way to being heard
		int64_t heard_us = 0; // .. heard then (Engine::now_us's clock)
		int step = 0, loop = 0;
		double beats = 0.0;
	};
	struct Inbound { // an event taken from a playback, its time to be heard worked out once its mix step is known
		FeluccaPlayback *p;
		fel_event_t e;
		uint64_t step;
		int64_t heard_us; // (-1: not yet)
	};
	std::vector<Pending> pend;
	std::vector<Inbound> inbox;
	std::vector<FelHeard> taken;
	std::vector<ObjectID> touched; // sources whose instance came or went (bound / unbound)
	uint32_t next_id = 0;
	double latency = 0.0; // AudioServer's output latency, seconds (asked once a second: it can be slow)
	int64_t latency_at = INT64_MIN, paused_at = 0;
	double heard_last = 0.0; // the playhead as heard, last worked out (it never goes back)
	bool hit_warned = false; // (on_hit on a part that is not DRUM: said once)
	std::vector<uint32_t> listening; // (deliver's: each live playback's, worked out before the lock)
	int64_t last_beat = -1; // the last whole beat on_beat said
	void forget(size_t p_index); // a pending action gone (its playback's pin with it)
	void refresh_latency(int64_t p_now);
	double heard_locked(int64_t p_now); // the playhead as heard at p_now (under the lock)
	int64_t heard_at_locked(double p_beats, int64_t p_now); // when a value of the timeline is heard (under the lock)
	const Ear *ear(Viewport *p_viewport);
	double reach(Node *p_node, bool &r_paused);

protected:
	static void _bind_methods();

public:
	static FeluccaConductor *get_singleton() { return singleton; }
	FeluccaConductor();
	~FeluccaConductor();

	void set_tempo(double p_bpm);
	double get_tempo() const;
	void play(); // the conductor's timeline moves
	void stop(); // the conductor's timeline stands (the sources stay where they are)
	bool is_running() const;
	double get_playhead(); // the conductor's timeline as heard, in beats: continuous, it never goes back
	int get_bar(); // .. its 4-beat bars from 0, 1-based
	int get_beat(); // .. its beat in the bar, 1 .. 4
	double get_rendered_playhead() const; // (hidden) the conductor's timeline where the next mix step starts
	int get_pool_size() const;
	void boot_pool(); // boot every pool instance now (the Felucca autoload does it when the game starts)
	int get_bound_count() const;
	int get_booted_count() const; // (these three hidden: the tests')
	int64_t get_mix_steps() const;
	int64_t get_paused_mix_steps() const;
	int64_t get_mix_usec() const;
	int64_t get_boot_usec() const;

	// the extension's own
	void ensure_node(); // (a source entering the tree, or a call above, at run time)
	void adopt_node(Node *p_node); // the autoload's node, entering the tree
	void add_playback(FeluccaPlayback *p_playback);
	void remove_playback(FeluccaPlayback *p_playback);
	void resolve_pending(); // the node, last in each frame's process: this frame's play()s to their origin,
	void binding_pass(); // .. then copies to the sources pinned, in range, and to the others while some are free
	void deliver(); // .. then bound / unbound, the stopped sources' actions cancelled, and what has been heard

	// scheduling: FelSource's schedule / cancel; a playback's, binding (its actions into the instance, before it is
	// mixed), its instance coming or going; FeluccaSchedule.cancel
	Ref<FeluccaSchedule> schedule(Node *p_source, FeluccaPlayback *p_playback, const Ref<FeluccaWhen> &p_when,
			const Ref<FeluccaAction> &p_action, const StringName &p_tag, const Callable &p_callback);
	Ref<FeluccaSnapshot> heard_snapshot(FeluccaPlayback *p_playback); // FeluccaSource.get_snapshot
	void cancel_tag(Node *p_source, const StringName &p_tag);
	void cancel_id(uint32_t p_id);
	void send_pending(FeluccaPlayback *p_playback, const fel_core_t *p_core);
	void touch(ObjectID p_source) { touched.push_back(p_source); }
	void forget_all(); // (the game ends: what is pending, without a signal)
	void tree_paused(bool p_paused); // the node, first to hear the tree pause
};

class FeluccaConductorNode : public Node {
	GDCLASS(FeluccaConductorNode, Node);

protected:
	static void _bind_methods() {}
	void _notification(int p_what);
};

} // namespace godot
