// L2 (docs/DESIGN.md §4): the conductor's timeline, the plan of each mix step, the pool of cores. The clock effect
// writes the plan at the end of each mix step and the playbacks render the next step against it, both on Godot's audio
// thread. What the main thread changes (tempo, running, paused) it writes only under AudioServer::lock(), which the
// audio driver holds for every mix: a mix step sees all of a change or none of it, and every instance sees the same.
#pragma once

#include "fel_core.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace felucca {

// the mix step about to be mixed: the conductor's timeline moves linearly from b0 to b1 over its frames
struct Plan {
	double b0 = 0.0, b1 = 0.0, tempo = 120.0;
	uint32_t frames = 512;
};

class Engine {
public:
	static Engine &get();

	// audio thread reads; main thread writes under the lock (then republish())
	Plan plan;
	double tempo = 120.0;
	bool running = false; // the conductor's timeline moves (FeluccaConductor.play / stop)
	bool paused = false; // the tree is paused (FeluccaConductor's node, D7)
	double mix_rate = 44100.0;

	// audio thread counts, main thread reads
	std::atomic<uint64_t> steps{ 0 }, paused_steps{ 0 };

	// the clock effect, at the end of each mix step: the conductor's timeline moved over it; the next step's plan
	void audio_step(uint32_t p_frames);
	// main thread, under the lock: the pending step's plan after tempo, running or paused changed
	void republish();

	// the pool (main thread): every copy boots once, when the game starts (boot_all: 8 x 5-6 ms, the Felucca
	// autoload's); a free one for a source (booted now if it had not been: no autoload); -1 when every one is taken
	int acquire();
	void release(int p_slot); // cut (silent for its next source: fel_core.h) and free
	void boot_all();
	const fel_core_t *core(int p_slot) const;
	int size() const { return int(slots.size()); }
	int bound() const;
	int booted() const;

private:
	struct Slot {
		const fel_core_t *core = nullptr;
		bool booted = false, used = false;
	};
	std::vector<Slot> slots;
	void init();
	double delta() const;
};

} // namespace felucca
