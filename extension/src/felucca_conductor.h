// The conductor (D6, D7, §5.2): FeluccaConductor, an engine singleton as Input is (FeluccaConductor.tempo = 140.0),
// owns the timeline, its tempo and transport and the pool's playbacks. Its node, FeluccaConductorNode, is the project's
// autoload (addons/felucca/felucca.tscn): Godot tells a pause to the root's children in the order they were added
// (Node::_propagate_pause_notification walks data.children, not the child index), and autoloads are added before the
// main scene, so it hears the tree's PAUSED / UNPAUSED before any scene node: it stops the timeline and pauses every
// bound source's player in one AudioServer::lock(), and Godot's own per-node pauses after it change nothing. It also
// owns the clock effect's muted bus, and resolves each frame's play()s to one origin. Without the autoload one is added
// at run time, after the scene: then the pause is not atomic (a warning says so).
#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>

#include <vector>

namespace godot {

class FeluccaPlayback;

class FeluccaConductor : public Object {
	GDCLASS(FeluccaConductor, Object);
	static FeluccaConductor *singleton;
	ObjectID node;
	std::vector<FeluccaPlayback *> live, pending; // main thread (live: in the order of their play()s)
	uint64_t seq = 0;

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
	double get_beats() const; // the conductor's timeline where the next mix step starts
	int get_pool_size() const;
	void boot_pool(); // boot every pool instance now (the Felucca autoload does it when the game starts)
	int get_bound_count() const;
	int get_booted_count() const;
	int64_t get_mix_steps() const;
	int64_t get_paused_mix_steps() const;

	// the extension's own
	void ensure_node(); // (a source entering the tree, or a call above, at run time)
	void adopt_node(Node *p_node); // the autoload's node, entering the tree
	void add_playback(FeluccaPlayback *p_playback);
	void remove_playback(FeluccaPlayback *p_playback);
	void resolve_pending(); // the node, last in each frame's process: this frame's play()s to their origin,
	void bind_waiting(); // .. then free copies to the playbacks waiting for one, the first come first
	void tree_paused(bool p_paused); // the node, first to hear the tree pause
};

class FeluccaConductorNode : public Node {
	GDCLASS(FeluccaConductorNode, Node);

protected:
	static void _bind_methods() {}
	void _notification(int p_what);
};

} // namespace godot
