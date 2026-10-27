#include "felucca_schedule.h"

#include "felucca_conductor.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace felucca;

namespace godot {

// ---------------------------------------------------------------- FeluccaWhen ---

static Ref<FeluccaWhen> step_when(const Ref<FeluccaPart> &p_part, int p_loops, int p_step, FeluccaWhen::Late p_late) {
	if (p_part.is_null()) {
		UtilityFunctions::push_error("FeluccaWhen: no part.");
		return Ref<FeluccaWhen>();
	}
	if (p_step < 1 || p_step > 64 || p_loops < 0) {
		UtilityFunctions::push_error("FeluccaWhen: step ", p_step, " is not one (1 to 64).");
		return Ref<FeluccaWhen>();
	}
	Ref<FeluccaWhen> w;
	w.instantiate();
	w->values = p_part->source_values();
	w->track = p_part->get_track();
	w->step = p_step - 1;
	w->loops = p_loops;
	w->late = p_late;
	return w;
}

Ref<FeluccaWhen> FeluccaWhen::next_step(const Ref<FeluccaPart> &p_part, int p_step, Late p_late) {
	return step_when(p_part, 0, p_step, p_late);
}

Ref<FeluccaWhen> FeluccaWhen::next_loop(const Ref<FeluccaPart> &p_part, int p_step, Late p_late) {
	return step_when(p_part, 1, p_step, p_late);
}

Ref<FeluccaWhen> FeluccaWhen::loops_ahead(const Ref<FeluccaPart> &p_part, int p_loops, int p_step, Late p_late) {
	if (p_loops < 1) {
		UtilityFunctions::push_error("FeluccaWhen: loops_ahead() takes 1 loop or more.");
		return Ref<FeluccaWhen>();
	}
	return step_when(p_part, p_loops, p_step, p_late);
}

Ref<FeluccaWhen> FeluccaWhen::next_bar(int p_beat, Late p_late) {
	if (p_beat < 1 || p_beat > 4) {
		UtilityFunctions::push_error("FeluccaWhen: beat ", p_beat, " is not one of a bar (1 to 4).");
		return Ref<FeluccaWhen>();
	}
	Ref<FeluccaWhen> w;
	w.instantiate();
	w->bar = true;
	w->beat = p_beat - 1;
	w->late = p_late;
	return w;
}

void FeluccaWhen::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("next_step", "part", "step", "late"), &FeluccaWhen::next_step,
			DEFVAL(LATE_NEXT));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("next_loop", "part", "step", "late"), &FeluccaWhen::next_loop,
			DEFVAL(LATE_NEXT));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("loops_ahead", "part", "loops", "step", "late"),
			&FeluccaWhen::loops_ahead, DEFVAL(LATE_NEXT));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("next_bar", "beat", "late"), &FeluccaWhen::next_bar, DEFVAL(1),
			DEFVAL(LATE_NEXT));
	ClassDB::bind_method(D_METHOD("set_late", "late"), &FeluccaWhen::set_late);
	ClassDB::bind_method(D_METHOD("get_late"), &FeluccaWhen::get_late);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "late", PROPERTY_HINT_ENUM, "Next,Drop,Now"), "set_late", "get_late");
	BIND_ENUM_CONSTANT(LATE_NEXT);
	BIND_ENUM_CONSTANT(LATE_DROP);
	BIND_ENUM_CONSTANT(LATE_NOW);
}

// ---------------------------------------------------------------- FeluccaAction ---

static Ref<FeluccaAction> part_action(const Ref<FeluccaPart> &p_part, uint8_t p_kind, int32_t p_a, int32_t p_b, int32_t p_c) {
	Ref<FeluccaAction> a;
	a.instantiate();
	a->values = p_part->source_values();
	a->action = { p_kind, uint8_t(p_part->get_track()), p_a, p_b, p_c };
	return a;
}

Ref<FeluccaAction> FeluccaAction::hit(const Ref<FeluccaPart> &p_part, int p_lane, int p_velocity) {
	if (p_part.is_null() || p_lane < 0 || p_lane >= FEL_LANES || p_velocity < 1 || p_velocity > 127) {
		UtilityFunctions::push_error("FeluccaAction.hit(): a part, a lane (FeluccaPart.Lane) and a velocity (1 to 127).");
		return Ref<FeluccaAction>();
	}
	return part_action(p_part, FEL_DO_HIT, p_lane, p_velocity, 0);
}

Ref<FeluccaAction> FeluccaAction::note(const Ref<FeluccaPart> &p_part, int p_note, int p_velocity, int p_steps) {
	if (p_part.is_null() || p_note < 0 || p_note > 127 || p_velocity < 1 || p_velocity > 127 || p_steps < 1 || p_steps > 64) {
		UtilityFunctions::push_error("FeluccaAction.note(): a part, a note (0 to 127), a velocity (1 to 127) and steps (1 to 64).");
		return Ref<FeluccaAction>();
	}
	return part_action(p_part, FEL_DO_NOTE, p_note, p_velocity, p_steps);
}

Ref<FeluccaAction> FeluccaAction::note_layered(const Ref<FeluccaPart> &p_part, int p_note, int p_velocity, int p_steps) {
	if (p_part.is_null() || p_note < 0 || p_note > 127 || p_velocity < 1 || p_velocity > 127 || p_steps < 1 || p_steps > 64) {
		UtilityFunctions::push_error("FeluccaAction.note_layered(): a part, a note (0 to 127), a velocity (1 to 127) and steps (1 to 64).");
		return Ref<FeluccaAction>();
	}
	return part_action(p_part, FEL_DO_NOTE_OVER, p_note, p_velocity, p_steps);
}

static Ref<FeluccaAction> mask(const Ref<FeluccaPart> &p_part, int p_count, int p_unit, const char *p_name) {
	if (p_part.is_null() || p_count < 1 || p_count > 4096) {
		UtilityFunctions::push_error("FeluccaAction.", p_name, "(): a part and a count (1 to 4096).");
		return Ref<FeluccaAction>();
	}
	return part_action(p_part, FEL_DO_MASK, p_count, p_unit, 0);
}

Ref<FeluccaAction> FeluccaAction::mask_steps(const Ref<FeluccaPart> &p_part, int p_steps) {
	return mask(p_part, p_steps, FEL_SPAN_STEPS, "mask_steps");
}

Ref<FeluccaAction> FeluccaAction::mask_loops(const Ref<FeluccaPart> &p_part, int p_loops) {
	return mask(p_part, p_loops, FEL_SPAN_LOOPS, "mask_loops");
}

Ref<FeluccaAction> FeluccaAction::mask_bars(const Ref<FeluccaPart> &p_part, int p_bars) {
	return mask(p_part, p_bars, FEL_SPAN_BARS, "mask_bars");
}

// a part's own parameter (FeluccaPart.Param: its id), or its engine's (an engine view's Param: (engine + 1) x 256 + the
// id, every engine numbering its eight alike): only the part's engine's, and only while it plays it (the core drops it
// at its step else)
Ref<FeluccaAction> FeluccaAction::set_param(const Ref<FeluccaPart> &p_part, int p_param, int p_value) {
	const int engine = p_param >= 256 ? p_param / 256 - 1 : -1, id = p_param >= 256 ? p_param % 256 : p_param;
	const ApiClass *view = nullptr;
	for (int k = 0; k < API_VIEW_COUNT && engine >= 0; k++) {
		view = API_VIEWS[k].engine == engine ? &API_VIEWS[k] : view;
	}
	const ApiProp *p = engine < 0 ? api_prop(API_PART, id) : view ? api_prop(*view, id) : nullptr;
	if (p_part.is_null() || !p) {
		UtilityFunctions::push_error("FeluccaAction.set_param(): a part and a parameter (FeluccaPart.Param, or its engine view's Param).");
		return Ref<FeluccaAction>();
	}
	if (engine >= 0 && p_part->get_engine() != engine) {
		UtilityFunctions::push_error("FeluccaAction.set_param(): a parameter of ", view->class_name, ", but part ",
				p_part->get_index(), " plays another engine.");
		return Ref<FeluccaAction>();
	}
	return part_action(p_part, FEL_DO_SET, id, CLAMP(p_value, p->min, p->max), engine);
}

Ref<FeluccaAction> FeluccaAction::mark() {
	Ref<FeluccaAction> a;
	a.instantiate();
	a->action = { FEL_DO_MARK, 0, 0, 0, 0 };
	return a;
}

void FeluccaAction::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("hit", "part", "lane", "velocity"), &FeluccaAction::hit, DEFVAL(100));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("note", "part", "note", "velocity", "steps"), &FeluccaAction::note,
			DEFVAL(100), DEFVAL(1));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("note_layered", "part", "note", "velocity", "steps"),
			&FeluccaAction::note_layered, DEFVAL(100), DEFVAL(1));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("mask_steps", "part", "steps"), &FeluccaAction::mask_steps);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("mask_loops", "part", "loops"), &FeluccaAction::mask_loops);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("mask_bars", "part", "bars"), &FeluccaAction::mask_bars);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("set_param", "part", "param", "value"), &FeluccaAction::set_param);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("mark"), &FeluccaAction::mark);
}

// ---------------------------------------------------------------- FeluccaEvent, FeluccaSchedule ---

void FeluccaEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_tag"), &FeluccaEvent::get_tag);
	ClassDB::bind_method(D_METHOD("get_action"), &FeluccaEvent::get_action);
	ClassDB::bind_method(D_METHOD("get_part"), &FeluccaEvent::get_part);
	ClassDB::bind_method(D_METHOD("get_step"), &FeluccaEvent::get_step);
	ClassDB::bind_method(D_METHOD("get_loop"), &FeluccaEvent::get_loop);
	ClassDB::bind_method(D_METHOD("get_playhead"), &FeluccaEvent::get_playhead);
	ClassDB::bind_method(D_METHOD("get_late_by"), &FeluccaEvent::get_late_by);
	ClassDB::bind_method(D_METHOD("is_dropped"), &FeluccaEvent::is_dropped);
	const uint32_t ro = PROPERTY_USAGE_NONE;
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "tag", PROPERTY_HINT_NONE, "", ro), "", "get_tag");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "action", PROPERTY_HINT_NONE, "", ro, "FeluccaAction"), "", "get_action");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "part", PROPERTY_HINT_NONE, "", ro), "", "get_part");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "step", PROPERTY_HINT_NONE, "", ro), "", "get_step");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "loop", PROPERTY_HINT_NONE, "", ro), "", "get_loop");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "playhead", PROPERTY_HINT_NONE, "", ro), "", "get_playhead");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "late_by", PROPERTY_HINT_NONE, "", ro), "", "get_late_by");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dropped", PROPERTY_HINT_NONE, "", ro), "", "is_dropped");
}

void FeluccaSchedule::cancel() {
	if (FeluccaConductor *c = FeluccaConductor::get_singleton(); c && pending) {
		c->cancel_id(id);
	}
}

void FeluccaSchedule::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_tag"), &FeluccaSchedule::get_tag);
	ClassDB::bind_method(D_METHOD("is_pending"), &FeluccaSchedule::is_pending);
	ClassDB::bind_method(D_METHOD("cancel"), &FeluccaSchedule::cancel);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "tag", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_tag");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pending", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "is_pending");
	ADD_SIGNAL(MethodInfo("triggered", PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "FeluccaEvent")));
}

} // namespace godot
