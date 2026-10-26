// Scheduling (DESIGN.md §5.2, M6): what a script schedules on a playing source and what it hears back. FeluccaWhen (a
// step of one of the source's parts, n passes on, or the conductor's next bar), FeluccaAction (a hit, a note, a
// parameter, a mark), FeluccaSchedule (what FeluccaSource.schedule() returns: its own heard signal), FeluccaEvent (what
// the signals carry). The conductor keeps the pending actions (FeluccaConductor::schedule) and hands them to the
// source's instance (fel_core_t.schedule), the events back when heard.
#pragma once

#include "fel_core.h"
#include "felucca_api.h"

#include <godot_cpp/classes/ref_counted.hpp>

#include <memory>

namespace godot {

class FeluccaWhen : public RefCounted {
	GDCLASS(FeluccaWhen, RefCounted);

protected:
	static void _bind_methods();

public:
	// a target found passed when it is reached (a FOLLOW resume, a bar gone by): its next occurrence, dropped, now
	enum Late { LATE_NEXT, LATE_DROP, LATE_NOW };

	bool bar = false; // next_bar: the conductor's bars, resolved when scheduled
	int track = 0, step = 0, loops = 0, beat = 0; // (0-based step, 0-based beat in the bar)
	Late late = LATE_NEXT;
	std::shared_ptr<FelValues> values; // the source the part is of (next_bar: none)

	static Ref<FeluccaWhen> next_step(const Ref<FeluccaPart> &p_part, int p_step, Late p_late);
	static Ref<FeluccaWhen> next_loop(const Ref<FeluccaPart> &p_part, int p_step, Late p_late);
	static Ref<FeluccaWhen> loops_ahead(const Ref<FeluccaPart> &p_part, int p_loops, int p_step, Late p_late);
	static Ref<FeluccaWhen> next_bar(int p_beat, Late p_late);
	void set_late(Late p_late) { late = p_late; }
	Late get_late() const { return late; }
};

class FeluccaAction : public RefCounted {
	GDCLASS(FeluccaAction, RefCounted);

protected:
	static void _bind_methods();

public:
	fel_action_t action = {};
	std::shared_ptr<FelValues> values; // the source the part is of (mark: none)

	static Ref<FeluccaAction> hit(const Ref<FeluccaPart> &p_part, int p_lane, int p_velocity);
	static Ref<FeluccaAction> note(const Ref<FeluccaPart> &p_part, int p_note, int p_velocity, int p_steps);
	// (not set(): Object's)
	static Ref<FeluccaAction> set_param(const Ref<FeluccaPart> &p_part, int p_param, int p_value);
	static Ref<FeluccaAction> mark();
};

// what the signals carry: what was scheduled, where it happened, when it was heard
class FeluccaEvent : public RefCounted {
	GDCLASS(FeluccaEvent, RefCounted);

protected:
	static void _bind_methods();

public:
	StringName tag;
	Ref<FeluccaAction> action;
	int part = 0, step = 0, loop = 0; // part 1 .. 4 (0: none, a mark on a bar), step 1-based, loop the part's pass
	double beats = 0.0, late_by = 0.0; // the conductor's timeline there; seconds since it was heard
	bool dropped = false;

	StringName get_tag() const { return tag; }
	Ref<FeluccaAction> get_action() const { return action; }
	int get_part() const { return part; }
	int get_step() const { return step; }
	int get_loop() const { return loop; }
	double get_beats() const { return beats; }
	double get_late_by() const { return late_by; }
	bool is_dropped() const { return dropped; }
};

// one scheduled action: heard(event) when it is (or dropped); never when cancelled
class FeluccaSchedule : public RefCounted {
	GDCLASS(FeluccaSchedule, RefCounted);

protected:
	static void _bind_methods();

public:
	uint32_t id = 0;
	StringName tag;
	bool pending = true;

	StringName get_tag() const { return tag; }
	bool is_pending() const { return pending; }
	void cancel();
};

} // namespace godot

VARIANT_ENUM_CAST(FeluccaWhen::Late);
