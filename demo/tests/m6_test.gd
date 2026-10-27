# M6's checks in Godot 4.7.2 (extension/run_tests.sh), parts 2 and 3: scheduling from scripts, the signals and the
# snapshot as heard (DESIGN.md §5.2, §6.3, §7), each piece on its own: what schedule() and the targets and actions take and refuse; a mark on the next loop and its
# event, heard (on the handle too); four hits, heard a step's length apart as they play; next_step in this pass or the
# next; the conductor's next bar; cancel(tag), a handle's cancel(), stop(); the late policies after a FOLLOW resume
# (NEXT, DROP: dropped at once, NOW); set_param kept for the next binding; pinning (actions pending, pinned = true,
# more pinned than engines: a warning, counted by extension/run_tests.sh); bound / unbound; the conductor's playhead
# (never back), bar / beat and on_beat; a part's on_step / on_loop; the snapshot as heard; callbacks, in their order
# with the signals; a part's notes: on_note / on_noteoff in pairs, a DRUM part's on_hit, the warning for on_hit on
# another; replace and mask: a note in place of a step's own, one layered over it, a loop masked but for a scheduled
# kick, a mask of 0 refused. (The knock and the door
# together are the demo's; the core's timing: core/tests/test_sched.c.)
extends Node

var fails := 0
var project: FeluccaProject = preload("res://music/test-4.felucca")   # part 1 ANALOG, part 4 DRUM
var got: Array[FeluccaEvent] = []       # every scheduled signal, in the order emitted,
var got_us: Array[int] = []             # .. when (Time.get_ticks_usec),
var got_on: Array[Node] = []            # .. on which source
var bound_n: Dictionary[Node, int] = {}
var unbound_n: Dictionary[Node, int] = {}

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		fails += 1

func wait(seconds: float) -> void:
	await get_tree().create_timer(seconds, true, false, true).timeout

func until(cond: Callable, limit := 5.0) -> bool:
	var t0 := Time.get_ticks_msec()
	while not cond.call():
		if Time.get_ticks_msec() - t0 > limit * 1000:
			return false
		await get_tree().process_frame
	return true

func watch(s: Node) -> void:
	bound_n[s] = 0
	unbound_n[s] = 0
	s.connect("schedule_triggered", func(_tag: StringName, e: FeluccaEvent) -> void:
		got.append(e)
		got_us.append(Time.get_ticks_usec())
		got_on.append(s))
	s.connect("bound", func() -> void: bound_n[s] += 1)
	s.connect("unbound", func() -> void: unbound_n[s] += 1)

# drums: 16 steps of 1/16, a 2 s pass at 120 BPM
func source(id: String) -> FeluccaSource:
	var s := FeluccaSource.new()
	s.name = id
	s.project = project
	s.quantize = FeluccaSource.QUANTIZE_NOW
	s.part_4.seq_length = 16
	s.part_4.seq_division = FeluccaPart.SeqDivision.SEQ_DIVISION_1_16
	add_child(s)
	watch(s)
	return s

func started(id: String) -> FeluccaSource:
	var s := source(id)
	s.play()
	await until(func() -> bool: return s.is_bound())
	await wait(0.2)
	return s

func heard(tag: StringName) -> Array[FeluccaEvent]:
	return got.filter(func(e: FeluccaEvent) -> bool: return e.tag == tag)

func stop_all() -> void:
	for c in get_children():
		if c is FeluccaSource or c is FeluccaSource2D:
			c.call("stop")
	await wait(0.3)

func _ready() -> void:
	print("mix rate %d Hz, pool of %d" % [AudioServer.get_mix_rate(), FeluccaConductor.pool_size])
	FeluccaConductor.tempo = 120.0
	FeluccaConductor.play()
	await refused()
	await mark_next_loop()
	await knock()
	await next_step()
	await next_bar()
	await cancelled()
	await late()
	await set_param()
	await pinning()
	await heard_time()
	await callbacks()
	await notes()
	await replace_mask()
	await stop_all()
	check(FeluccaConductor.get_bound_count() == 0, "all stopped: every engine free")
	print("m6_test: %d failed" % fails)
	get_tree().quit(1 if fails else 0)

# ---- what is refused (each says why: extension/run_tests.sh counts the errors)
func refused() -> void:
	var a := source("Refused")
	var other := source("Other")
	var drums := a.part_4
	check(FeluccaWhen.next_step(null, 1) == null and FeluccaWhen.next_loop(drums, 0) == null and
		FeluccaWhen.next_bar(5) == null and FeluccaWhen.loops_ahead(drums, 0, 1) == null,
		"FeluccaWhen: no part, step 0, beat 5, 0 loops ahead: null")
	check(FeluccaAction.hit(drums, 8) == null and FeluccaAction.note(drums, 60, 0) == null and
		FeluccaAction.set_param(drums, 9999, 0) == null, "FeluccaAction: lane 9, velocity 0, a parameter that is none: null")
	var when := FeluccaWhen.next_loop(drums, 1)
	check(when.late == FeluccaWhen.LATE_NEXT and FeluccaWhen.next_loop(drums, 1, FeluccaWhen.LATE_DROP).late == FeluccaWhen.LATE_DROP,
		"late: NEXT by default, or as given")
	check(a.schedule(when, FeluccaAction.mark()) == null, "schedule() on a source that is not playing: null")
	a.play()
	check(a.schedule(FeluccaWhen.next_loop(other.part_4, 1), FeluccaAction.mark()) == null and
		a.schedule(when, FeluccaAction.hit(other.part_4, FeluccaPart.Lane.LANE_KICK)) == null,
		"schedule() with a part of another source: null")
	check(a.schedule(null, FeluccaAction.mark()) == null, "schedule() without a target: null")
	await stop_all()

# ---- a mark on the next loop: its event when heard, on the source and on the handle
func mark_next_loop() -> void:
	var a := await started("Mark")
	check(bound_n[a] == 1, "bound: once (%d)" % bound_n[a])
	var mark := FeluccaAction.mark()
	AudioServer.lock()
	var snap := a._get_rendered_snapshot()
	var h := a.schedule_with_tag(FeluccaWhen.next_loop(a.part_4, 1), mark, &"door")
	AudioServer.unlock()
	var loop := snap.loops[3]
	check(h != null and h.pending and h.tag == &"door", "schedule(): a pending handle with its tag")
	var e: FeluccaEvent = await h.triggered
	await get_tree().process_frame                # (the source's schedule_triggered comes after the handle's)
	var on := heard(&"door")
	check(on.size() == 1 and on[0] == e, "heard: the handle's triggered and the source's schedule_triggered, one event")
	check(e.tag == &"door" and e.action == mark and e.part == 4 and e.step == 1 and e.loop == loop + 1 and not e.dropped,
		"the event: its tag, its action, part 4, step 1 of the next pass (%d, pass %d playing)" % [e.loop, loop])
	check(abs(e.playhead - snap.origin - 4.0 * loop) < 0.01,
		"at the conductor's %.4f: the origin %.4f and %d passes of 4 beats" % [e.playhead, snap.origin, loop])
	check(e.late_by >= 0.0 and e.late_by < 0.05 and not h.pending, "emitted %.1f ms after it was heard; the handle done" % (e.late_by * 1000))
	a.stop()
	check(await until(func() -> bool: return unbound_n[a] == 1, 2.0), "stopped: unbound, once")

# ---- four hits: heard a step's length apart as they play (each part the time it is heard: §6.3)
func knock() -> void:
	var a := await started("Knock")
	var from := got.size()
	for step: int in [1, 5, 9, 13]:
		a.schedule_with_tag(FeluccaWhen.next_loop(a.part_4, step), FeluccaAction.hit(a.part_4, FeluccaPart.Lane.LANE_KICK), &"knock")
	await until(func() -> bool: return got.size() >= from + 4, 6.0)
	var k := heard(&"knock")
	var steps := k.map(func(e: FeluccaEvent) -> int: return e.step)
	check(k.size() == 4 and steps == [1, 5, 9, 13] and k.all(func(e: FeluccaEvent) -> bool: return e.loop == k[0].loop),
		"four hits on steps %s of one pass" % [steps])
	var gaps: Array[float] = []
	for i in range(from + 1, got.size()):
		gaps.append((got_us[i] - got_us[i - 1]) / 1e6)
	check(gaps.size() == 3 and gaps.all(func(g: float) -> bool: return abs(g - 0.5) < 0.03),
		"heard 0.5 s apart (4 steps of 1/16 at 120 BPM): %s" % [gaps.map(func(g: float) -> String: return "%.3f" % g)])
	check(k.all(func(e: FeluccaEvent) -> bool: return e.late_by >= 0.0 and e.late_by < 0.05), "each emitted within 50 ms of being heard")
	await stop_all()

# ---- next_step: in this pass while the step is still to come, else the next
func next_step() -> void:
	var a := await started("NextStep")
	AudioServer.lock()
	var snap := a._get_rendered_snapshot()
	var cur := snap.steps[3] - 1                     # (0-based here)
	var ahead := (cur + 6) % 16
	a.schedule_with_tag(FeluccaWhen.next_step(a.part_4, ahead + 1), FeluccaAction.mark(), &"ahead")
	a.schedule_with_tag(FeluccaWhen.next_step(a.part_4, cur + 1), FeluccaAction.mark(), &"current")
	AudioServer.unlock()
	var loop := snap.loops[3]
	await until(func() -> bool: return heard(&"ahead").size() + heard(&"current").size() == 2, 5.0)
	var e1 := heard(&"ahead")
	var e2 := heard(&"current")
	check(e1.size() == 1 and e1[0].step == ahead + 1 and e1[0].loop == loop + (0 if ahead > cur else 1),
		"step %d with step %d of pass %d playing: pass %d" % [ahead + 1, cur + 1, loop, loop + (0 if ahead > cur else 1)])
	check(e2.size() == 1 and e2[0].step == cur + 1 and e2[0].loop == loop + 1, "the step playing: the next pass")
	await stop_all()

# ---- the conductor's next bar
func next_bar() -> void:
	var a := await started("Bar")
	AudioServer.lock()
	var b := FeluccaConductor._get_rendered_playhead()
	a.schedule_with_tag(FeluccaWhen.next_bar(), FeluccaAction.mark(), &"bar1")
	a.schedule_with_tag(FeluccaWhen.next_bar(3), FeluccaAction.mark(), &"bar3")
	AudioServer.unlock()
	var bar := floorf(b / 4.0) * 4.0 + 4.0
	await until(func() -> bool: return heard(&"bar3").size() == 1, 6.0)
	var e1 := heard(&"bar1")
	var e3 := heard(&"bar3")
	check(e1.size() == 1 and abs(e1[0].playhead - bar) < 0.005 and e1[0].part == 0,
		"next_bar(): beat %.3f, the bar after %.3f (part 0: none)" % [e1[0].playhead if e1 else 0.0, b])
	check(e3.size() == 1 and abs(e3[0].playhead - bar - 2.0) < 0.005, "next_bar(3): its third beat")
	await stop_all()

# ---- cancel(tag), a handle's cancel(), stop(): nothing emitted
func cancelled() -> void:
	var a := await started("Cancel")
	var when := FeluccaWhen.next_loop(a.part_4, 1)
	var x1 := a.schedule_with_tag(when, FeluccaAction.mark(), &"x")
	var x2 := a.schedule_with_tag(when, FeluccaAction.mark(), &"x")
	var y := a.schedule_with_tag(when, FeluccaAction.mark(), &"y")
	var z := a.schedule_with_tag(when, FeluccaAction.mark(), &"z")
	a.cancel(&"x")
	z.cancel()
	check(not x1.pending and not x2.pending and not z.pending and y.pending, "cancel(&\"x\"), a handle's cancel(): no longer pending")
	await until(func() -> bool: return not y.pending, 5.0)
	await wait(0.3)
	check(heard(&"y").size() == 1 and heard(&"x").is_empty() and heard(&"z").is_empty(), "only the one left is heard")
	var b := await started("Stopped")
	var s := b.schedule_with_tag(FeluccaWhen.next_loop(b.part_4, 1), FeluccaAction.mark(), &"stopped")
	b.stop()
	await wait(2.6)
	check(heard(&"stopped").is_empty() and not s.pending, "stop(): its actions cancelled, nothing heard")
	await stop_all()

# ---- late: its own pause past the targets, then a FOLLOW resume (the source relocates): NEXT at the step's next
# start, DROP dropped at once, NOW at the first step after
func late() -> void:
	var a := await started("Late")
	AudioServer.lock()
	var loop := a._get_rendered_snapshot().loops[3]
	var hn := a.schedule_with_tag(FeluccaWhen.loops_ahead(a.part_4, 1, 5), FeluccaAction.mark(), &"late_next")
	var hd := a.schedule_with_tag(FeluccaWhen.loops_ahead(a.part_4, 1, 5, FeluccaWhen.LATE_DROP), FeluccaAction.mark(), &"late_drop")
	var hw := a.schedule_with_tag(FeluccaWhen.loops_ahead(a.part_4, 1, 5, FeluccaWhen.LATE_NOW), FeluccaAction.mark(), &"late_now")
	AudioServer.unlock()
	await wait(0.1)
	a.stream_paused = true
	await wait(3.0)                     # (pass 1's step 5 is 2.5 s from step 1 of pass 0)
	check(heard(&"late_next").is_empty() and heard(&"late_drop").is_empty() and a.is_bound(),
		"paused past the targets: nothing heard, the engine kept (pinned)")
	var t0 := Time.get_ticks_usec()
	a.stream_paused = false
	await until(func() -> bool: return not hn.pending and not hd.pending and not hw.pending, 4.0)
	var d := heard(&"late_drop")
	var w := heard(&"late_now")
	var n := heard(&"late_next")
	var at := func(e: FeluccaEvent) -> float: return (got_us[got.find(e)] - t0) / 1e6
	check(d.size() == 1 and d[0].dropped and at.call(d[0]) < 0.3, "DROP: dropped, at once (%.3f s after the resume)" % [at.call(d[0]) if d else -1.0])
	check(w.size() == 1 and not w[0].dropped and at.call(w[0]) < 0.3,
		"NOW: on the first step after the resume (step %d, %.3f s)" % [w[0].step if w else 0, at.call(w[0]) if w else -1.0])
	check(n.size() == 1 and not n[0].dropped and n[0].step == 5 and n[0].loop > loop + 1,
		"NEXT: step 5 at its next start (pass %d, not %d)" % [n[0].loop if n else -1, loop + 1])
	await stop_all()

# ---- set_param: applied on its step, kept as what scripts set (the next binding plays it)
func set_param() -> void:
	var a := await started("SetParam")
	var before := a.part_1.level
	var h := a.schedule_with_tag(FeluccaWhen.next_loop(a.part_4, 1), FeluccaAction.set_param(a.part_1, FeluccaPart.Param.PARAM_LEVEL, 77), &"level")
	check(a.part_1.level == before, "pending: part 1's level still %d" % before)
	await h.triggered
	check(a.part_1.level == 77, "heard: part 1's level 77 (%d)" % a.part_1.level)
	a.stop()
	await until(func() -> bool: return not a.is_bound(), 2.0)
	a.play()
	await until(func() -> bool: return a.is_bound(), 2.0)
	check(a.part_1.level == 77, "played again: still 77 (%d)" % a.part_1.level)
	await stop_all()

# ---- pinning: every engine taken by sources in range; one out of range with an action pending takes one (from the
# source in range bound last), keeps it until heard, then gives it back; pinned = true likewise; more pinned than
# engines: the farthest waits, a warning
func pinning() -> void:
	var crowd: Array[FeluccaSource] = []
	for i in FeluccaConductor.pool_size:
		var s := source("Crowd%d" % i)
		crowd.append(s)
		s.play()
	var all_bound := func() -> bool: return crowd.all(func(s: FeluccaSource) -> bool: return s.is_bound())
	await until(all_bound, 3.0)
	var far := FeluccaSource2D.new()
	far.name = "Far"
	far.project = project
	far.quantize = FeluccaSource.QUANTIZE_NOW
	far.max_distance = 100.0
	far.position = Vector2(100000, 0)
	add_child(far)
	watch(far)
	far.play()
	await wait(0.5)
	check(not far.is_bound(), "out of range, every engine taken: it waits")
	var h := far.schedule_with_tag(FeluccaWhen.next_loop(far.part_4, 1), FeluccaAction.mark(), &"far")
	check(await until(func() -> bool: return far.is_bound(), 2.0), "an action pending: pinned, it takes an engine")
	var gave := crowd.filter(func(s: FeluccaSource) -> bool: return not s.is_bound())
	check(gave.size() == 1, "from one source in range (%s)" % [gave.map(func(s: Node) -> String: return s.name)])
	var e: FeluccaEvent = await h.triggered
	check(e.part == 4 and e.step == 1, "its action heard")
	check(await until(func() -> bool: return not far.is_bound() and all_bound.call(), 3.0),
		"heard: the pin ends, and the source in range takes its engine back")
	check(bound_n[far] == 1 and unbound_n[far] == 1 and gave.size() == 1 and bound_n[gave[0]] == 2 and unbound_n[gave[0]] == 1,
		"bound / unbound: Far %d / %d, %s %d / %d" % [bound_n[far], unbound_n[far], gave[0].name if gave else "?",
		bound_n[gave[0]] if gave else 0, unbound_n[gave[0]] if gave else 0])
	far.pinned = true
	check(await until(func() -> bool: return far.is_bound(), 2.0), "pinned = true: an engine")
	await wait(1.0)
	check(far.is_bound(), ".. kept")
	far.pinned = false
	check(await until(func() -> bool: return not far.is_bound() and all_bound.call(), 3.0), "pinned = false: given back")
	for s in crowd:
		s.pinned = true
	far.pinned = true
	await wait(1.0)
	check(not far.is_bound() and all_bound.call(), "9 pinned, 8 engines: the farthest waits (a warning)")
	await stop_all()

# ---- the playhead as heard: never back, a little behind the rendered one; bar / beat its counter; on_beat each beat
# once, in order; a part's on_step / on_loop, 1-based, in order, a step apart; the snapshot as heard agrees with them
func heard_time() -> void:
	var a := await started("Heard")
	var prev := FeluccaConductor.playhead
	var back := 0
	var lag := 0.0
	for f in 60:
		await get_tree().process_frame
		var h := FeluccaConductor.playhead
		back += int(h < prev)
		prev = h
		lag = maxf(lag, FeluccaConductor._get_rendered_playhead() - h)
	check(back == 0 and lag >= 0.0 and lag < 0.25,
		"playhead: never back in 60 frames, behind the rendered one by %.3f beats at most" % lag)
	var p := FeluccaConductor.playhead
	var counted := (FeluccaConductor.bar - 1) * 4 + FeluccaConductor.beat - 1
	check(counted == int(p) or counted == int(p) + 1, "bar %d, beat %d: the playhead %.3f counted" % [FeluccaConductor.bar,
		FeluccaConductor.beat, p])

	var beats: Array[int] = []
	var beat_us: Array[int] = []
	var on_beat := func(bar: int, beat: int) -> void:
		beats.append((bar - 1) * 4 + beat - 1)
		beat_us.append(Time.get_ticks_usec())
	var steps: Array[Vector2i] = []
	var step_us: Array[int] = []
	var loops: Array[int] = []
	var agree := [0]                    # the snapshot, read in on_step: its step
	var on_step := func(step: int, loop: int) -> void:
		steps.append(Vector2i(step, loop))
		step_us.append(Time.get_ticks_usec())
		var s := a.get_snapshot()
		agree[0] += int(s.steps[3] == step and s.loops[3] == loop)
	var on_loop := func(loop: int) -> void: loops.append(loop)
	FeluccaConductor.on_beat.connect(on_beat)
	a.part_4.on_step.connect(on_step)
	a.part_4.on_loop.connect(on_loop)
	await wait(4.4)
	FeluccaConductor.on_beat.disconnect(on_beat)
	a.part_4.on_step.disconnect(on_step)
	a.part_4.on_loop.disconnect(on_loop)

	var in_order := range(1, beats.size()).all(func(i: int) -> bool: return beats[i] == beats[i - 1] + 1)
	var gaps := range(1, beat_us.size()).map(func(i: int) -> float: return (beat_us[i] - beat_us[i - 1]) / 1e6)
	check(beats.size() >= 7 and in_order and gaps.all(func(g: float) -> bool: return abs(g - 0.5) < 0.04),
		"on_beat: %d beats, each once and in order, 0.5 s apart (%s)" % [beats.size(),
		", ".join(PackedStringArray(gaps.map(func(g: float) -> String: return "%.3f" % g)))])
	var follows := range(1, steps.size()).all(func(i: int) -> bool:
		var q := steps[i - 1]
		var n := steps[i]
		return n.x == q.x % 16 + 1 and n.y == q.y + int(n.x == 1))
	var sgaps := range(1, step_us.size()).map(func(i: int) -> float: return (step_us[i] - step_us[i - 1]) / 1e6)
	check(steps.size() >= 30 and follows and sgaps.all(func(g: float) -> bool: return abs(g - 0.125) < 0.04),
		"on_step: %d steps, 1 to 16 and round, the loop counted on step 1, 0.125 s apart" % steps.size())
	var starts := steps.filter(func(v: Vector2i) -> bool: return v.x == 1).map(func(v: Vector2i) -> int: return v.y)
	check(loops == starts and loops.size() >= 2 and loops[0] >= 1, "on_loop: on each step 1, its loop (%s)" % [loops])
	check(agree[0] == steps.size(), "the snapshot, read in on_step: that step and loop (%d of %d)" % [agree[0], steps.size()])

	var s := a.get_snapshot()
	var h := FeluccaConductor.playhead
	check(s.bound and s.playing and s.steps.size() == 4 and s.progress[3] >= 0.0 and s.progress[3] < 1.0 and
		abs(s.playhead - (h - s.origin)) < 0.01, "the snapshot: part 4 at step %d (%.2f into it), %.3f beats since its step 1"
		% [s.steps[3], s.progress[3], s.playhead])
	var rising := 0
	var last := -1.0
	var last_step := s.steps[3]
	for f in 20:
		await get_tree().process_frame
		var q := a.get_snapshot()
		if q.steps[3] == last_step:
			rising += int(q.progress[3] >= last)
		else:
			rising += 1
		last = q.progress[3]
		last_step = q.steps[3]
	check(rising == 20, "its progress rises through each step, frame by frame")
	var idle := source("Idle")
	check(idle.get_snapshot().steps.is_empty() and not idle.get_snapshot().bound, "a source not playing: no steps")
	await stop_all()

# ---- callbacks: schedule(when, action, callback), schedule_with_tag(when, action, tag, callback); at one moment the
# part's on_step, then the callback, the handle's triggered, the source's schedule_triggered; a callable whose object
# is gone is skipped
func callbacks() -> void:
	var a := await started("Callbacks")
	var order: Array[String] = []
	var on_step := func(step: int, _loop: int) -> void:
		if step == 5:
			order.append("on_step 5")
	a.part_4.on_step.connect(on_step)
	var h := a.schedule(FeluccaWhen.next_step(a.part_4, 5), FeluccaAction.mark(),
		func(e: FeluccaEvent) -> void: order.append("callback %d" % e.step))
	h.triggered.connect(func(_e: FeluccaEvent) -> void: order.append("triggered"))
	var all := func(_tag: StringName, _e: FeluccaEvent) -> void: order.append("schedule_triggered")
	a.schedule_triggered.connect(all)
	await until(func() -> bool: return order.size() >= 4, 4.0)
	a.part_4.on_step.disconnect(on_step)
	check(order.slice(0, 4) == ["on_step 5", "callback 5", "triggered", "schedule_triggered"] and h.tag == &"",
		"one moment, in order: %s" % [order.slice(0, 4)])
	var tagged: Array[FeluccaEvent] = []
	var t := a.schedule_with_tag(FeluccaWhen.next_step(a.part_4, 9), FeluccaAction.mark(), &"cb",
		func(e: FeluccaEvent) -> void: tagged.append(e))
	await t.triggered
	check(t.tag == &"cb" and tagged.size() == 1 and tagged[0].tag == &"cb", "schedule_with_tag: the tag on the handle and the event")
	var gone := Node.new()
	var g := a.schedule(FeluccaWhen.next_step(a.part_4, 13), FeluccaAction.mark(), Callable(gone, &"queue_free"))
	gone.free()
	await g.triggered
	check(not g.pending, "a callable whose object is gone: skipped (any error would be printed by extension/run_tests.sh)")
	a.schedule_triggered.disconnect(all)
	await stop_all()

# ---- notes: a part's on_note and on_noteoff, in pairs; a DRUM part's on_hit right after its on_note; a scheduled hit
# heard as a hit (KICK, its velocity) before its callback; a synth's scheduled note: on_note, on_noteoff two steps on, no
# on_hit; on_hit on a part that is not DRUM: never, and a warning (extension/run_tests.sh counts it)
func notes() -> void:
	var a := await started("Notes")
	var drums := a.part_4
	var synth := a.part_1
	var order: Array[String] = []
	var held := {}
	var off_alone := [0]
	var hit_alone := [0]
	var synth_hits := [0]
	var synth_us: Array[int] = []
	var synth_note := [-1]               # (the scheduled one: its velocity, 90, says which; the part's scale may move it)
	var noted := func(part: int, note: int, velocity: int) -> void:
		order.append("note %d %d %d" % [part, note, velocity])
		held[Vector2i(part, note)] = int(held.get(Vector2i(part, note), 0)) + 1
		if part == 1 and velocity == 90:
			synth_note[0] = note
			synth_us.append(Time.get_ticks_usec())
	var released := func(part: int, note: int) -> void:
		var k := Vector2i(part, note)
		off_alone[0] += int(int(held.get(k, 0)) == 0)
		held[k] = maxi(int(held.get(k, 0)) - 1, 0)
		if part == 1 and note == synth_note[0] and synth_us.size() == 1:
			synth_us.append(Time.get_ticks_usec())
	var hit := func(lane: int, velocity: int) -> void:
		var last: String = "" if order.is_empty() else order[-1]
		hit_alone[0] += int(not last.begins_with("note 4 ") or not last.ends_with(" %d" % velocity))
		order.append("hit %d %d" % [lane, velocity])
	drums.on_note.connect(func(n: int, v: int) -> void: noted.call(4, n, v))
	drums.on_noteoff.connect(func(n: int) -> void: released.call(4, n))
	drums.on_hit.connect(hit)
	synth.on_note.connect(func(n: int, v: int) -> void: noted.call(1, n, v))
	synth.on_noteoff.connect(func(n: int) -> void: released.call(1, n))
	synth.on_hit.connect(func(_l: int, _v: int) -> void: synth_hits[0] += 1)
	var kick := a.schedule(FeluccaWhen.next_loop(drums, 1), FeluccaAction.hit(drums, FeluccaPart.Lane.LANE_KICK),
		func(_e: FeluccaEvent) -> void: order.append("callback"))
	a.schedule(FeluccaWhen.next_loop(drums, 5), FeluccaAction.note(synth, 99, 90, 2))
	await kick.triggered
	await wait(1.0)
	var at := order.find("callback")
	var kick_at := order.slice(0, maxi(at, 0)).rfind("hit %d 100" % FeluccaPart.Lane.LANE_KICK)
	check(at > 0 and kick_at >= 0 and at - kick_at <= 4,
		"a scheduled hit: heard as KICK (velocity 100), at its moment and before its callback (%s)" % [order.slice(kick_at, at + 1)])
	var hits := order.filter(func(e: String) -> bool: return e.begins_with("hit "))
	check(hits.size() >= 2 and hit_alone[0] == 0, "on_hit: %d hits, each right after its on_note on the DRUM part" % hits.size())
	check(synth_us.size() == 2 and abs((synth_us[1] - synth_us[0]) / 1e6 - 0.25) < 0.05 and synth_hits[0] == 0,
		"a synth's note: on_note (99 on its scale: %d, velocity 90), on_noteoff %.3f s on (2 steps: 0.25), no on_hit" %
		[synth_note[0], (synth_us[1] - synth_us[0]) / 1e6 if synth_us.size() == 2 else -1.0])
	check(off_alone[0] == 0, "on_noteoff only for a note heard on (%d not), though connected mid-note" % off_alone[0])
	await stop_all()

# ---- replace and mask (each case at the core: core/tests/test_sched.c E). The synth part's own notes, heard over a
# pass, say which of its steps plays one: a note scheduled there next is heard in place of it, one layered the pass
# after with it; the drum part's next loop masked but for a kick scheduled in it, its own hits the loop after
func replace_mask() -> void:
	var a := source("Replace")
	a.part_1.seq_length = 16
	a.part_1.seq_division = FeluccaPart.SeqDivision.SEQ_DIVISION_1_16
	a.play()
	await until(func() -> bool: return a.is_bound())
	var synth := a.part_1
	var drums := a.part_4
	var heard_at := {}                  # part -> {Vector2i(loop, step): [Vector2i(note, velocity)]}
	for part: FeluccaPart in [synth, drums]:
		var at := {}
		heard_at[part.index] = at
		var now := [Vector2i(-1, -1)]
		part.on_step.connect(func(step: int, loop: int) -> void: now[0] = Vector2i(loop, step))
		part.on_note.connect(func(note: int, velocity: int) -> void:
			if not at.has(now[0]):
				at[now[0]] = []
			(at[now[0]] as Array).append(Vector2i(note, velocity)))
	await wait(2.3)                     # (a pass of 16 x 1/16 at 120 BPM: 2 s)
	var own := -1                       # a step of the synth's with a note of its own
	var own_notes: Array = []
	for k: Vector2i in heard_at[1]:
		if own < 0 and ((heard_at[1] as Dictionary)[k] as Array).size() > 0:
			own = k.y
			own_notes = ((heard_at[1] as Dictionary)[k] as Array).map(func(v: Vector2i) -> int: return v.x)
	check(own >= 1, "the synth part plays a note of its own on step %d (%s)" % [own, own_notes])
	var r := a.schedule(FeluccaWhen.next_step(synth, own), FeluccaAction.note(synth, 99, 90))
	var e1: FeluccaEvent = await r.triggered
	var l := a.schedule(FeluccaWhen.next_step(synth, own), FeluccaAction.note_layered(synth, 99, 90))
	var e2: FeluccaEvent = await l.triggered
	await wait(0.2)
	var replaced: Array = (heard_at[1] as Dictionary).get(Vector2i(e1.loop, e1.step), [])
	var layered: Array = (heard_at[1] as Dictionary).get(Vector2i(e2.loop, e2.step), [])
	check(replaced.size() == 1 and (replaced[0] as Vector2i).y == 90, "note(): heard in place of the step's own (%s)" % [replaced])
	check(layered.size() == own_notes.size() + 1 and layered.any(func(v: Vector2i) -> bool: return v.y == 90) and
		own_notes.all(func(n: int) -> bool: return layered.any(func(v: Vector2i) -> bool: return v.x == n)),
		"note_layered(): heard with the step's own (%s)" % [layered])

	var m := a.schedule(FeluccaWhen.next_loop(drums, 1), FeluccaAction.mask_loops(drums, 1))
	a.schedule(FeluccaWhen.next_loop(drums, 9), FeluccaAction.hit(drums, FeluccaPart.Lane.LANE_KICK, 77))
	var em: FeluccaEvent = await m.triggered
	await wait(4.3)                     # (that loop, and the next)
	var in_loop := func(loop: int) -> Array:
		var out := []
		for k: Vector2i in heard_at[4]:
			if k.x == loop:
				out.append_array((heard_at[4] as Dictionary)[k])
		return out
	var masked: Array = in_loop.call(em.loop)
	var after: Array = in_loop.call(em.loop + 1)
	check(masked == [Vector2i(36, 77)], "mask_loops(drums, 1): its own hits silent, the kick scheduled in it heard (%s)" % [masked])
	check(after.size() >= 2 and after.any(func(v: Vector2i) -> bool: return v.y != 77), "the loop after: its own hits (%d)" % after.size())
	check(FeluccaAction.mask_loops(drums, 0) == null, "mask_loops(drums, 0): refused")
	await stop_all()
