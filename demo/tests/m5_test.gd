# M5's checks in Godot 4.7.2 (extension/run_tests.sh), the pool's groundwork (DESIGN.md §7): every copy boots when the
# game starts; a playing source keeps its place in the music without a copy and gets one when one is free (the first
# come first, for now: the scoring comes next), joining the music where it is; a source deleted frees its copy; a
# source's own pause while it waits for one. (That a reused copy carries no tail: core/tests/test_cut.c.)
extends Node

var fails := 0
var project: FeluccaProject = preload("res://music/solar_signal.felucca")
var tol := 0                       # samples a position may differ by (0: exact)

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		fails += 1

func wait(seconds: float) -> void:
	await get_tree().create_timer(seconds, true, false, true).timeout

func source(join := FeluccaSource.JOIN_FOLLOW) -> FeluccaSource:
	var s := FeluccaSource.new()
	s.project = project
	s.quantize = FeluccaSource.QUANTIZE_NOW
	s.join = join
	add_child(s)
	return s

func snaps(a: Array[FeluccaSource]) -> Array[FeluccaSnapshot]:   # read in one mix step (one lock)
	var r: Array[FeluccaSnapshot] = []
	AudioServer.lock()
	for s in a:
		r.append(s.get_snapshot())
	AudioServer.unlock()
	return r

# a and b at one place in the music (within tol samples; a step's end within tol: one has crossed it, the other not)
func same_music(a: FeluccaSnapshot, b: FeluccaSnapshot) -> bool:
	if not (a.bound and b.bound and a.playing and b.playing and a.origin == b.origin):
		return false
	for t in 4:
		if a.steps[t] == b.steps[t]:
			if abs(a.positions[t] - b.positions[t]) > tol:
				return false
		elif tol == 0 or (a.positions[t] > tol and b.positions[t] > tol):
			return false
	return abs(a.beats - b.beats) < 0.005

func describe(s: FeluccaSnapshot) -> String:
	return "%s, origin %.2f, %.4f beats, steps %s, positions %s" % ["bound" if s.bound else "unbound", s.origin, s.beats,
		s.steps, s.positions]

# frames until the source has a copy (-1: none within limit); waited_ms: how long that was
var waited_ms := 0
func frames_until_bound(s: FeluccaSource, limit := 120) -> int:
	var t0 := Time.get_ticks_msec()
	for f in limit:
		if s.is_bound():
			waited_ms = Time.get_ticks_msec() - t0
			return f
		await get_tree().process_frame
	return -1

func _ready() -> void:
	tol = 0 if AudioServer.get_mix_rate() == 44100.0 else 160
	var n := FeluccaConductor.pool_size
	print("mix rate %d Hz, pool of %d" % [AudioServer.get_mix_rate(), n])
	check(FeluccaConductor.get_booted_count() == n, "every copy booted when the game started (%d of %d), before any play()"
		% [FeluccaConductor.get_booted_count(), n])
	var plain := FeluccaSource2D.new()
	check(plain.quantize == FeluccaSource.QUANTIZE_BEAT and plain.join == FeluccaSource.JOIN_FOLLOW,
		"a new source quantizes to the beat and follows, by default")
	plain.free()
	FeluccaConductor.tempo = project.bpm
	FeluccaConductor.play()

	# ---- a source started in a frame has its copy at the end of it (started in _ready, before the first frame: the
	# first frame's pass)
	var first := source()
	first.play()
	check(not first.is_bound(), "play(): no copy yet (the conductor binds last in the frame)")
	var f := await frames_until_bound(first)
	check(f >= 0 and f <= 2, "bound by the first frame's end (%d frames)" % f)
	first.stop()
	await wait(0.2)
	check(FeluccaConductor.get_bound_count() == 0, "stopped: its copy free again (%d bound)" % FeluccaConductor.get_bound_count())

	# ---- one more source than copies, started in one frame: the first n bound, the last waits, silently in time
	var group: Array[FeluccaSource] = []
	for i in n + 1:
		group.append(source())
	for s in group:
		s.play()
	await get_tree().process_frame
	await wait(1.0)
	var p := snaps(group)
	var first_n := range(n).all(func(i: int) -> bool: return p[i].bound)
	check(first_n and not p[n].bound and FeluccaConductor.get_bound_count() == n,
		"%d sources started in one frame: the first %d bound, the last waits (%s)" % [n + 1, n, describe(p[n])])
	check(range(1, n).all(func(i: int) -> bool: return same_music(p[0], p[i])), "the %d bound ones in step" % n)

	# ---- a copy freed by a stop goes to the one waiting, which joins the music where it is
	group[3].stop()
	f = await frames_until_bound(group[n])
	await wait(0.3)
	p = snaps(group)
	check(f >= 0 and same_music(p[0], p[n]), "one stopped: the waiting one bound %d frames (%d ms) after, in step with the others (%s | %s)"
		% [f, waited_ms, describe(p[0]), describe(p[n])])

	# ---- a source deleted frees its copy too
	var late := source()
	late.play()
	await wait(0.2)
	check(not late.is_bound(), "every copy taken: a source started now waits")
	group[5].queue_free()
	f = await frames_until_bound(late)
	await wait(0.3)
	var q := snaps([group[0], late])
	check(f >= 0, "a bound source deleted (queue_free): the waiting one bound %d frames (%d ms) after" % [f, waited_ms])
	check(q[1].bound and q[1].playing and abs(q[1].beats - (FeluccaConductor.beats - q[1].origin)) < 0.02,
		"it plays at its own origin, where the timeline is (%s; the timeline at %.3f)" % [describe(q[1]), FeluccaConductor.beats])

	# ---- its own pause while it waits: FOLLOW keeps its origin, HOLD's moves by the pause
	var follow := source(FeluccaSource.JOIN_FOLLOW)
	var hold := source(FeluccaSource.JOIN_HOLD)
	follow.play()
	hold.play()
	await wait(0.6)
	check(not follow.is_bound() and not hold.is_bound(), "two more wait")
	var b0 := FeluccaConductor.beats
	follow.stream_paused = true
	hold.stream_paused = true
	await wait(1.0)
	var paused := FeluccaConductor.beats - b0
	follow.stream_paused = false
	hold.stream_paused = false
	await wait(0.4)
	group[0].stop()
	group[1].stop()
	await frames_until_bound(follow)
	await frames_until_bound(hold)
	await wait(0.4)
	AudioServer.lock()
	q = snaps([follow, hold])
	var bt := FeluccaConductor.beats
	AudioServer.unlock()
	check(q[0].bound and q[1].bound and abs(q[1].origin - q[0].origin - paused) < 0.05,
		"paused 1 s by their own stream_paused while waiting, then bound: HOLD's origin %.3f beats later than FOLLOW's (the pause: %.3f; QUANTIZE_NOW: exactly)"
		% [q[1].origin - q[0].origin, paused])
	check(abs(q[0].beats - (bt - q[0].origin)) < 0.02 and abs(q[1].beats - (bt - q[1].origin)) < 0.02,
		".. each where its origin puts it (%.3f, %.3f beats; the timeline at %.3f, origins %.3f, %.3f)"
		% [q[0].beats, q[1].beats, bt, q[0].origin, q[1].origin])

	# ---- HOLD quantized to the beat: its origin moves by a whole number of beats, the nearest to the pause
	var beat := source(FeluccaSource.JOIN_HOLD)
	beat.quantize = FeluccaSource.QUANTIZE_BEAT
	follow.stop()
	beat.play()
	await frames_until_bound(beat)
	await wait(1.2)
	var o0 := beat.get_snapshot().origin
	b0 = FeluccaConductor.beats
	beat.stream_paused = true
	await wait(1.0)                    # (~1.1 beats: clear of 1.5, where the nearest beat changes)
	paused = FeluccaConductor.beats - b0
	beat.stream_paused = false
	await wait(0.3)
	var after := beat.get_snapshot()
	check(after.origin - o0 == roundf(paused) and fmod(after.origin, 1.0) == 0.0,
		"HOLD, QUANTIZE_BEAT: paused %.3f beats, its origin %.0f beats later, still on a beat (%.2f)" % [paused, after.origin - o0, after.origin])

	# ---- quantize and join are live: set on a source playing, its next resume uses them
	var live := source(FeluccaSource.JOIN_FOLLOW)
	live.quantize = FeluccaSource.QUANTIZE_BEAT
	beat.stop()
	live.play()
	await frames_until_bound(live)
	await wait(0.6)
	var lo := live.get_snapshot().origin
	live.join = FeluccaSource.JOIN_HOLD
	live.quantize = FeluccaSource.QUANTIZE_NOW
	b0 = FeluccaConductor.beats
	live.stream_paused = true
	await wait(0.8)
	paused = FeluccaConductor.beats - b0
	live.stream_paused = false
	await wait(0.3)
	var moved := live.get_snapshot().origin - lo
	check(abs(moved - paused) < 0.05 and fmod(moved, 1.0) != 0.0,
		"join and quantize set while it plays (FOLLOW -> HOLD, BEAT -> NOW): its resume holds, by the pause exactly (%.3f, the pause %.3f)"
		% [moved, paused])

	# ---- pitch_scale has no effect: a warning when it starts playing (extension/run_tests.sh counts it in the log; the
	# editor's node warning says the same, FelSource::ignored, and scripts cannot call it)
	var pitched := FeluccaSource2D.new()
	pitched.project = project
	add_child(pitched)
	pitched.pitch_scale = 2.0
	pitched.play()
	await wait(0.2)

	for s in get_children():
		if s is FeluccaSource or s is FeluccaSource2D:
			s.call("stop")
	await wait(0.3)
	check(FeluccaConductor.get_bound_count() == 0, "all stopped: every copy free (%d bound)" % FeluccaConductor.get_bound_count())
	print("m5_test: %d failed" % fails)
	get_tree().quit(1 if fails else 0)
