# M3's checks in Godot 4.7.2 (extension/run_tests.sh: headless, the Dummy audio driver, which mixes on a thread of its
# own under the driver lock, as the real drivers do):
#   T12  the clock effect runs once a mix step on its muted bus, during a tree pause too, with the conductor's timeline
#        standing still
#   T13  7 sources, 4000 nodes between each, started in one frame (one of them PROCESS_MODE_ALWAYS: paused all the
#        same, DESIGN.md D7): through 300 tree pause / unpause cycles, each pause stops every source before any scene
#        node hears it (the Felucca autoload hears it first), each unpause starts every one again, their music stays
#        at one position and their players' mixed frames keep their spacing
#   sync a source started later, quantize BAR, plays on the others' bars
#   own pause  stream_paused on a source holds through the tree's pause and unpause (godot #65743); FOLLOW resumes
#        where the music is, HOLD from where it stopped
#   import  the .felucca imports as the user's project; load_json reads the web app's .json at run time
# Positions are the instances' own, as rendered: at 44.1 kHz every mix step renders whole halves, so they agree exactly;
# at another rate (the playback resamples) what an instance has rendered ahead of what was heard, up to a half (128
# frames), differs between instances started at different times, and their blocks fall unlike: within a half and a
# block (160 samples) there.
class_name M3Test
extends Node

const N := 8
const FILLER := 4000
var fails := 0
var sources: Array[FeluccaSource] = []
var project: FeluccaProject = preload("res://music/solar_signal.felucca")
var tol := 0                       # samples a position may differ by (0: exact)
var told := 0                      # pauses / unpauses the probe heard
var told_late := 0                 # .. with a source not yet paused / unpaused by the conductor

class Probe extends Node:          # a scene node before the sources: by the time it hears the pause, the conductor
	var test: M3Test               # has paused (unpaused) every one
	func _notification(what: int) -> void:
		if what == NOTIFICATION_PAUSED or what == NOTIFICATION_UNPAUSED:
			test.told += 1
			for s in test.sources:
				if s.playing and not test.own.has(s) and s.stream_paused != (what == NOTIFICATION_PAUSED):
					test.told_late += 1
var own: Array = []                # sources paused by their own stream_paused (the probe leaves them be)

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		fails += 1

func wait(seconds: float) -> void:
	await get_tree().create_timer(seconds, true, false, true).timeout

func positions() -> Array[FeluccaSnapshot]:   # every source's music, read in one mix step (one lock)
	var a: Array[FeluccaSnapshot] = []
	AudioServer.lock()
	for s in sources:
		a.append(s._get_rendered_snapshot())
	AudioServer.unlock()
	return a

func mixed() -> Array:             # each player's mixed frames, in one mix step
	AudioServer.lock()
	var a := sources.map(func(s: FeluccaSource) -> int:
		return roundi(s.get_playback_position() * AudioServer.get_mix_rate()) if s.playing else -1)
	AudioServer.unlock()
	return a

func same_music(a: FeluccaSnapshot, b: FeluccaSnapshot) -> bool:
	if not (a.bound and b.bound and a.playing and b.playing):
		return false
	if tol == 0:
		return a.playhead == b.playhead and a.steps == b.steps and a.positions == b.positions and a.loops == b.loops
	for t in 4:                    # (a step's end within tol: one has crossed it, the other not)
		if not same_step(a, b, t):
			return false
	return abs(a.playhead - b.playhead) < 0.005

func same_step(a: FeluccaSnapshot, b: FeluccaSnapshot, t: int) -> bool:   # part t on one step (within tol)
	if a.steps[t] == b.steps[t]:
		return abs(a.positions[t] - b.positions[t]) <= tol
	return tol > 0 and (a.positions[t] <= tol or b.positions[t] <= tol)

func all_same(p: Array[FeluccaSnapshot], idx: Array) -> bool:
	for i in idx:
		if not same_music(p[idx[0]], p[i]):
			return false
	return true

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	tol = 0 if AudioServer.get_mix_rate() == 44100.0 else 160
	print("mix rate %d Hz, pool of %d" % [AudioServer.get_mix_rate(), FeluccaConductor.pool_size])
	FeluccaConductor.tempo = project.bpm
	FeluccaConductor.boot_pool()
	FeluccaConductor.play()
	await wait(0.2)
	check(get_tree().root.get_child(0) is FeluccaConductorNode, "the Felucca autoload is the conductor's node (the root's first child)")
	var probe := Probe.new()
	probe.test = self
	probe.process_mode = Node.PROCESS_MODE_PAUSABLE
	add_child(probe)

	# ---- import
	check(project.project_name == "SOLAR SIGNAL" and project.bpm == 68 and project.firmware == "FELUCCA v1.6.1"
		and project.engines == PackedStringArray(["PHASE", "PHASE", "LOFI", "DRUM"]) and project.data.size() == 3840,
		"import: the .felucca is the user's project (\"%s\", %d BPM, %s)" % [project.project_name, project.bpm,
		", ".join(project.engines)])
	var j := FeluccaProject.load_json("res://tests/project.json")
	check(j != null and j.data == project.data, "load_json: the web app's .json at run time, the same bytes")
	check(FeluccaProject.from_json("{\"format\": \"something else\"}") == null, "from_json: not a project -> null (its error above)")

	# ---- T12
	var b0 := FeluccaConductor._get_rendered_playhead()
	var s0 := FeluccaConductor._get_mix_steps()
	await wait(2.0)
	var steps := FeluccaConductor._get_mix_steps() - s0
	var moved := FeluccaConductor._get_rendered_playhead() - b0
	check(steps > 100 and abs(moved - 2.0 * 68 / 60) < 0.2,
		"T12 the clock: %d mix steps in 2 s, the conductor's timeline moved %.3f beats (%.3f at 68 BPM)"
		% [steps, moved, 2.0 * 68 / 60])
	get_tree().paused = true
	var p0 := FeluccaConductor._get_paused_mix_steps()
	var bp := FeluccaConductor._get_rendered_playhead()
	await wait(1.0)
	var ps := FeluccaConductor._get_paused_mix_steps() - p0
	check(ps > 50 and FeluccaConductor._get_rendered_playhead() == bp,
		"T12 paused: the clock ran (%d mix steps), the conductor's timeline stood (%.6f)"
		% [ps, FeluccaConductor._get_rendered_playhead() - bp])
	get_tree().paused = false

	# ---- T13
	for i in N:
		var holder := Node.new()
		holder.process_mode = Node.PROCESS_MODE_ALWAYS if i == 6 else Node.PROCESS_MODE_PAUSABLE
		add_child(holder)
		for k in FILLER:
			holder.add_child(Node.new())
		var s := FeluccaSource.new()
		s.name = "source%d" % i
		s.project = project
		s.quantize = FeluccaSource.QUANTIZE_BAR
		s.join = FeluccaSource.JOIN_HOLD if i == 1 else FeluccaSource.JOIN_FOLLOW
		holder.add_child(s)
		sources.append(s)
	for s: FeluccaSource in sources.slice(0, 7):  # one frame: one origin, on the conductor's next bar (source 7 joins later)
		s.play()
	var t0 := Time.get_ticks_msec()
	while not positions()[0].playing and Time.get_ticks_msec() - t0 < 6000:
		await wait(0.05)
	await wait(0.5)
	var start := positions()
	var seven := range(7)
	check(all_same(start, seven) and fmod(start[0].origin, 4.0) == 0.0, "T13 7 sources started in one frame: one position (beats %.4f, origin %.1f, on a bar)"
		% [start[0].playhead, start[0].origin])
	check(AudioServer.get_bus_peak_volume_left_db(0, 0) > -60.0, "T13 they sound (Master peak %.1f dB)" % AudioServer.get_bus_peak_volume_left_db(0, 0))
	var m0 := mixed()
	var out_of_step := 0
	var stuck := 0                 # cycles after which a source's music did not move on
	var running := 0               # .. or a source was not stopped while paused
	told = 0
	for k in 300:
		get_tree().paused = true
		await wait(randf_range(0.0, 0.03))
		var during := mixed()
		await wait(0.012)
		var still := mixed()
		for i in 7:
			running += int(not sources[i].stream_paused or still[i] != during[i])
		var before := positions()
		get_tree().paused = false
		await wait(randf_range(0.0, 0.03))
		var m := FeluccaConductor._get_mix_steps()    # (the Dummy driver mixes in bursts: wait for two mix steps)
		var t := Time.get_ticks_msec()
		while FeluccaConductor._get_mix_steps() < m + 2 and Time.get_ticks_msec() - t < 500:
			await wait(0.005)
		var after := positions()
		for i in 7:
			stuck += int(not after[i].playing or after[i].playhead <= before[i].playhead)
		if not all_same(after, seven):
			out_of_step += 1
	var m1 := mixed()
	check(told == 600 and told_late == 0, "T13 the pause reached every source before the scene's nodes (%d of %d told late)" % [told_late, told])
	check(running == 0 and stuck == 0, "T13 every pause stopped every source (the ALWAYS one too) and every unpause started it again (%d, %d not)"
		% [running, stuck])
	var spacing := 0
	for i in 7:
		spacing = max(spacing, abs((m1[i] - m1[0]) - (m0[i] - m0[0])))
	var p := positions()
	check(out_of_step == 0 and spacing == 0 and p[0].playhead > start[0].playhead,
		"T13 300 tree pause / unpause cycles: %d checks out of step; the players' spacing moved %d frames (music at %.3f beats)"
		% [out_of_step, spacing, p[0].playhead])

	# ---- a source started later, on the bar
	sources[7].play()
	t0 = Time.get_ticks_msec()
	while not positions()[7].playing and Time.get_ticks_msec() - t0 < 6000:
		await wait(0.05)
	await wait(0.3)
	p = positions()
	check(p[7].playing and fmod(p[7].origin, 4.0) == 0.0 and fmod(p[7].origin - p[2].origin, 4.0) == 0.0
		and p[7].steps[3] == p[2].steps[3] and abs(p[7].positions[3] - p[2].positions[3]) <= tol,
		"sync: started %.0f beats later, on a bar: the DRUM track (a bar long) on its step with the others (%d / %d)"
		% [p[7].origin - p[2].origin, p[7].steps[3], p[2].steps[3]])

	# ---- own pause: source 0 FOLLOW, source 1 HOLD; source 2 plays on
	own = [sources[0], sources[1]]
	var b_pause := FeluccaConductor._get_rendered_playhead()
	sources[0].stream_paused = true
	sources[1].stream_paused = true
	await wait(0.6)
	var held := positions()
	get_tree().paused = true
	await wait(0.3)
	get_tree().paused = false
	await wait(0.2)
	check(sources[0].stream_paused and sources[1].stream_paused, "own pause: still paused after the tree's pause and unpause (godot #65743)")
	p = positions()
	check(p[0].playhead == held[0].playhead and p[1].playhead == held[1].playhead and p[2].playhead > held[2].playhead,
		"own pause: their music stood (%.3f), the others' moved on (%.3f -> %.3f)" % [p[0].playhead, held[2].playhead, p[2].playhead])
	await wait(1.6)                    # (over 2 beats in all: HOLD's origin moves by a bar, these sources' quantize)
	var missed := FeluccaConductor._get_rendered_playhead() - b_pause
	sources[0].stream_paused = false
	sources[1].stream_paused = false
	own = []
	await wait(0.5)
	p = positions()
	check(same_music(p[0], p[2]), "own pause, FOLLOW: back where the music is (%.4f beats, as source 2's %.4f)" % [p[0].playhead, p[2].playhead])
	var gap: float = p[1].origin - p[2].origin
	check(gap == 4.0 * roundf(missed / 4.0) and gap > 0.0 and abs((p[2].playhead - p[1].playhead) - gap) < (0.005 if tol else 1.0 / 24000 + 1e-9),
		"own pause, HOLD: its origin later by the pause (%.3f beats) to the nearest bar, %.0f beats: on its bars still" % [missed, gap])

	# ---- a pausable source whose play() comes in a pause: its music does not start until the unpause (then on its
	# bar: these sources quantize BAR), and it has no copy in it (the conductor binds in its process, which the pause
	# stands too)
	sources[5].stop()
	await wait(0.3)
	get_tree().paused = true
	await wait(0.1)
	sources[5].play()
	await wait(0.8)
	var in_pause := sources[5]._get_rendered_snapshot()
	get_tree().paused = false
	t0 = Time.get_ticks_msec()
	while not sources[5]._get_rendered_snapshot().playing and Time.get_ticks_msec() - t0 < 6000:
		await wait(0.02)
	var after_pause := sources[5]._get_rendered_snapshot()
	check(not in_pause.bound and not in_pause.playing and after_pause.bound and after_pause.playing
		and fmod(after_pause.origin, 4.0) == 0.0 and after_pause.origin > FeluccaConductor._get_rendered_playhead() - 4.5,
		"play() in a pause: no copy and its music not started in it; bound after the unpause, on the next bar (origin %.1f)"
		% after_pause.origin)

	# ---- stop: the instance goes back to the pool
	var bound := FeluccaConductor.get_bound_count()
	sources[3].stop()
	await wait(0.3)
	check(FeluccaConductor.get_bound_count() == bound - 1, "stop: its instance back in the pool (%d -> %d bound)" % [bound, FeluccaConductor.get_bound_count()])

	print("m3_test: %d failed" % fails)
	for s in sources:
		s.stop()
	await wait(0.2)
	get_tree().quit(1 if fails else 0)
