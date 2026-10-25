#include "felucca_engine.h"

#include "fel_pool.h"

namespace felucca {

Engine &Engine::get() {
	static Engine e;
	if (e.slots.empty()) {
		e.init();
	}
	return e;
}

void Engine::init() {
	slots.resize(std::size_t(fel_pool_size()));
	for (int i = 0; i < fel_pool_size(); i++) {
		slots[std::size_t(i)].core = fel_pool_core(i);
	}
}

double Engine::delta() const {
	return running && !paused ? plan.frames * tempo / (60.0 * mix_rate) : 0.0;
}

void Engine::audio_step(uint32_t p_frames) {
	steps.fetch_add(1, std::memory_order_relaxed);
	if (paused) {
		paused_steps.fetch_add(1, std::memory_order_relaxed);
	}
	plan.frames = p_frames;
	plan.b0 = plan.b1;
	plan.tempo = tempo;
	plan.b1 = plan.b0 + delta();
}

void Engine::republish() {
	plan.tempo = tempo;
	plan.b1 = plan.b0 + delta();
}

int Engine::acquire() {
	for (std::size_t i = 0; i < slots.size(); i++) {
		Slot &s = slots[i];
		if (s.used) {
			continue;
		}
		if (!s.booted) {
			s.core->boot(); // (~1.5 s of device time, 5-6 ms here: M5 moves it to a worker)
			s.booted = true;
		}
		s.used = true;
		return int(i);
	}
	return -1;
}

void Engine::boot_all() {
	for (Slot &s : slots) {
		if (!s.booted && !s.used) {
			s.core->boot();
			s.booted = true;
		}
	}
}

void Engine::release(int p_slot) {
	if (p_slot < 0 || p_slot >= size()) {
		return;
	}
	slots[std::size_t(p_slot)].core->stop();
	slots[std::size_t(p_slot)].used = false;
}

const fel_core_t *Engine::core(int p_slot) const {
	return p_slot >= 0 && p_slot < size() ? slots[std::size_t(p_slot)].core : nullptr;
}

int Engine::bound() const {
	int n = 0;
	for (const Slot &s : slots) {
		n += s.used;
	}
	return n;
}

} // namespace felucca
