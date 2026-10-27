# M5's scene test (extension/run_tests.sh): the binding pass in a 2D scene like demo/main.tscn. Seventeen sources of
# range 600 px (the demo's four projects), twelve playing from the start, five more started later; the listener is a
# Camera2D (no AudioListener2D: the screen's centre) the script moves: a walk, a teleport, more sources in range than
# copies, a reshuffle among them, and a teleport back. At each stop, once settled, what Godot's own 2D players hear
# decides which sources must have a copy: every one in range, or the nearest ones when there are more than copies; the
# spare copies with sources out of range (lazy); every bound source where its origin puts it in the music. Run in the
# editor, it draws the sources (green: bound) and their ranges.
extends Node2D

const SETTLE := 0.6                # seconds a stop is given (a 2D player starts on Godot's physics tick, then one faded
                                   # mix step for an evicted copy; the Dummy driver mixes in bursts)
var fails := 0
var sources: Array[FeluccaSource2D] = []
@onready var cam: Camera2D = $Listener

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		fails += 1

func wait(seconds: float) -> void:
	await get_tree().create_timer(seconds, true, false, true).timeout

# where the listener is, as Godot's 2D players reckon it without an AudioListener2D: the screen's centre in the canvas
func centre() -> Vector2:
	var vp := get_viewport()
	return (vp.global_canvas_transform * vp.canvas_transform).affine_inverse() * (vp.get_visible_rect().size * 0.5)

func reach(s: FeluccaSource2D, at: Vector2) -> float:
	return s.global_position.distance_to(at) / s.max_distance

func playing() -> Array[FeluccaSource2D]:
	return sources.filter(func(s: FeluccaSource2D) -> bool: return s.playing)

func names(a: Array) -> String:
	return ", ".join(PackedStringArray(a.map(func(s: Node) -> String: return s.name)))

func bound_set() -> String:
	return "".join(PackedStringArray(sources.map(func(s: FeluccaSource2D) -> String: return "1" if s.is_bound() else "0")))

# where the source plays: where its origin puts it on the conductor's timeline (read in one mix step)
func in_time(s: FeluccaSource2D) -> bool:
	AudioServer.lock()
	var p := s._get_rendered_snapshot()
	var bt := FeluccaConductor._get_rendered_playhead()
	AudioServer.unlock()
	return p.bound and p.playing and abs(p.playhead - (bt - p.origin)) < 0.02

# the sources in range of the listener (at: where it is now, by default), the nearest first
func in_range(at := Vector2.INF) -> Array[FeluccaSource2D]:
	if at == Vector2.INF:
		at = centre()
	var a := playing().filter(func(s: FeluccaSource2D) -> bool: return reach(s, at) <= 1.0)
	a.sort_custom(func(x: FeluccaSource2D, y: FeluccaSource2D) -> bool: return reach(x, at) < reach(y, at))
	return a

# once settled: the nearest in range bound (all of them when there are no more than copies), the others in range not,
# every copy in use (the spare ones with sources out of range), every bound source in time
func settled(stop: String, expect_in: int) -> void:
	await wait(SETTLE)
	var n := FeluccaConductor.pool_size
	var near := in_range()
	var heard := near.slice(0, n)
	var waiting := near.slice(n)
	var all_playing := playing()
	check(near.size() == expect_in, "%s: %d in range (%s)" % [stop, near.size(), names(near)])
	var missing := heard.filter(func(s: FeluccaSource2D) -> bool: return not s.is_bound())
	var extra := waiting.filter(func(s: FeluccaSource2D) -> bool: return s.is_bound())
	var want := "every one in range" if waiting.is_empty() else "the %d nearest, %s waiting" % [n, names(waiting)]
	check(missing.is_empty() and extra.is_empty(), ".. bound: %s (unbound: [%s]; bound, should wait: [%s])"
		% [want, names(missing), names(extra)])
	check(FeluccaConductor.get_bound_count() == mini(n, all_playing.size()),
		".. every copy in use: %d bound of %d playing (lazy: the spare ones out of range)" % [FeluccaConductor.get_bound_count(), all_playing.size()])
	var late := all_playing.filter(func(s: FeluccaSource2D) -> bool: return s.is_bound() and not in_time(s))
	check(late.is_empty(), ".. every bound source where its origin puts it in the music (not: [%s])" % names(late))

func walk(to: Vector2, seconds: float) -> void:
	var t := create_tween()
	t.tween_property(cam, "position", to, seconds)
	await t.finished

# frames (and ms) until every source in range of a listener at `at` has a copy (-1: not within limit). (At the listener's
# new place: the camera moves the canvas, where the pass finds the listener, on its own process, a frame later)
func frames_until_heard(at: Vector2, limit := 120) -> Array[int]:
	var t0 := Time.get_ticks_msec()
	for f in limit:
		if in_range(at).all(func(s: FeluccaSource2D) -> bool: return s.is_bound()):
			return [f, Time.get_ticks_msec() - t0]
		await get_tree().process_frame
	return [-1, Time.get_ticks_msec() - t0]

func _ready() -> void:
	for c in get_children():
		if c is FeluccaSource2D:
			sources.append(c)
	var n := FeluccaConductor.pool_size
	print("mix rate %d Hz, pool of %d, %d sources" % [AudioServer.get_mix_rate(), n, sources.size()])
	FeluccaConductor.tempo = sources[0].project.bpm
	FeluccaConductor.play()

	# ---- the start: twelve autoplay, the five around the listener in range; the three spare copies out of range
	await settled("start, at (0, 0)", 5)

	# ---- a walk east, between two groups, then into the middle one
	await walk(Vector2(650, 0), 1.0)
	await settled("walked to (650, 0)", 2)
	await walk(Vector2(1200, 0), 1.0)
	await settled("walked to (1200, 0)", 3)

	# ---- a teleport: the four sources now in range take the copies of the farthest out of range, all at once
	cam.position = Vector2(2400, 0)
	var r: Array[int] = await frames_until_heard(cam.position)
	check(r[0] >= 0, "teleported to (2400, 0): every source in range has a copy %d frames (%d ms) later" % [r[0], r[1]])
	await settled(".. settled", 4)

	# ---- five more started around the listener: nine in range, more than copies; the farthest of them waits
	for s in sources:
		if s.name.begins_with("Crowd"):
			s.play()
	await settled("five more started: 9 in range, more than copies", 9)

	# ---- the listener moves among them: the one that waited is the nearest now, and takes the farthest one's copy
	await walk(Vector2(2300, -200), 1.0)
	await settled("walked to (2300, -200): the order changed", 9)

	# ---- standing still: no copy changes hands
	var before := bound_set()
	var changes := 0
	for f in 60:
		await get_tree().process_frame
		var now := bound_set()
		if now != before:
			changes += 1
			before = now
	check(changes == 0, "standing still for 60 frames: no copy changed hands (%d changes)" % changes)

	# ---- one of them stops: eight in range, every one bound
	($Crowd2 as FeluccaSource2D).stop()
	await settled("Crowd2 stopped: 8 in range", 8)

	# ---- a teleport back: the first five take copies again, from the farthest out of range
	cam.position = Vector2.ZERO
	r = await frames_until_heard(cam.position)
	check(r[0] >= 0, "teleported back to (0, 0): every source in range has a copy %d frames (%d ms) later" % [r[0], r[1]])
	await settled(".. settled", 5)

	for s in sources:
		s.stop()
	await wait(0.3)
	check(FeluccaConductor.get_bound_count() == 0, "all stopped: every copy free (%d bound)" % FeluccaConductor.get_bound_count())
	print("m5_scene_test: %d failed" % fails)
	get_tree().quit(1 if fails else 0)

func _process(_delta: float) -> void:
	queue_redraw()

func _draw() -> void:
	for s in sources:
		var c := Color.GREEN if s.is_bound() else (Color.GRAY if s.playing else Color.DIM_GRAY)
		draw_arc(s.position, s.max_distance, 0.0, TAU, 64, Color(c, 0.15))
		draw_circle(s.position, 16.0, c)
	draw_circle(cam.position, 10.0, Color.WHITE)
