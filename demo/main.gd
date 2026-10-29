# The demo: a project made on an FM-1 ("SOLAR SIGNAL", Felucca 1.6.1) in two sources, the cellar on the
# left and the hallway on the right, at the project's own tempo; the tempo eases to 80 BPM and back. Space pauses the
# game (and all of its music, at once); headless, it prints where both sources are. Each source is drawn with its range
# and a ring: green while it has an engine, orange in range without one, grey playing out of range, dark stopped; its
# squares pulse on each pass of its drum part (part 4's on_loop: on 16 steps of 1/16, a bar). Above it, a small square a
# part (1 to 4, left to right) pulses on each note the part starts (on_note; a DRUM part's on_hit), coloured by the
# note's place in the octave (C red, round the hues to B), lighter an octave up from middle C, darker an octave down,
# and greys once the part's notes are all released (on_noteoff). Number keys schedule on the Cellar: 1 snares on the
# next bar's four beats, 2 a fill on the drums' last four steps, 3 three notes up the major scale from a random root
# (C3 .. C4) on part 3 on the next bar's first beats, 4 the same layered, 5 a random note (C3 .. C4) on part 1 two
# steps on; with Shift each also masks what it plays over (the bar, the fill's steps, the notes' steps). 6 tunes part 2
# (SAMPLE's tune) a semitone a step for four steps, two steps on (up, or down near the top), and back on the fifth.
extends Node2D

@onready var cellar: FeluccaSource2D = $Cellar
#@onready var upstairs: FeluccaSource2D = $Upstairs
@onready var hallway: FeluccaSource2D = $Hallway
var sources: Array[FeluccaSource2D] = []
var hud: Label

const HELP := "1 snares on the next bar   2 a fill at the drums' loop's end   3 three notes up the major scale on part 3   4 the same, layered   5 a note on part 1   (Shift: also mask)   6 part 2 up a semitone a step for four steps, back on the fifth"
const MAJOR: Array[int] = [0, 2, 4]          # three steps up the major scale
const DIV_BEATS: Array[float] = [1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 2.0, 4.0, 8.0, 16.0]   # SeqDivision, in beats
const NAMES: Array[String] = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var project := cellar.project
	print("project \"%s\" (%s), %d BPM: %s" % [project.project_name, project.firmware, project.bpm, ", ".join(project.engines)])
	FeluccaConductor.tempo = project.bpm
	FeluccaConductor.boot_pool()
	FeluccaConductor.play()
	cellar.play()
	#hallway.play()
	var t := create_tween().set_loops()
	t.tween_method(func(bpm: float) -> void: FeluccaConductor.tempo = bpm, float(project.bpm), 80.0, 8.0).set_delay(4.0)
	t.tween_method(func(bpm: float) -> void: FeluccaConductor.tempo = bpm, 80.0, float(project.bpm), 8.0).set_delay(4.0)
	var timer := Timer.new()
	timer.wait_time = 1.0
	timer.autostart = true
	timer.timeout.connect(_report)
	add_child(timer)
	for c in get_children():
		if c is FeluccaSource2D:
			sources.append(c)
			_pulse_on_loops(c)
			_note_squares(c)
	var layer := CanvasLayer.new()
	add_child(layer)
	hud = Label.new()
	hud.position = Vector2(16, 64)
	hud.text = HELP
	layer.add_child(hud)

# each pass of the source's drum part, as heard: its squares grow, then ease back
func _pulse_on_loops(s: FeluccaSource2D) -> void:
	var rects: Array[ColorRect] = []
	for c in s.get_children():
		if c is ColorRect:
			rects.append(c)
	s.part_4.on_loop.connect(func(_loop: int) -> void:
		for r: ColorRect in rects:
			_pulse(r, 1.6, 0.4))

# a square a part above the source: each note it starts, its colour and a pulse (a DRUM part's: on its hit); grey once
# its notes are all released
func _note_squares(s: FeluccaSource2D) -> void:
	for i in 4:
		var part: FeluccaPart = s.parts[i]
		var r := ColorRect.new()
		r.size = Vector2(12, 12)
		r.position = Vector2(-36 + i * 20, -62)
		r.color = Color.GRAY
		s.add_child(r)
		var held := [0]
		var drum := part.engine == FeluccaPart.Engine.ENGINE_DRUM
		part.on_note.connect(func(note: int, _velocity: int) -> void:
			held[0] += 1
			r.color = _note_color(note)
			if not drum:
				_pulse(r, 1.8, 0.25))
		part.on_noteoff.connect(func(_note: int) -> void:
			held[0] = maxi(held[0] - 1, 0)
			if held[0] == 0:
				r.color = Color.GRAY)
		if drum:
			part.on_hit.connect(func(_lane: int, _velocity: int) -> void: _pulse(r, 1.8, 0.25))

# a note's colour: its hue by its place in the octave, lighter or darker by its octave from middle C's (60)
func _note_color(note: int) -> Color:
	var c := Color.from_hsv((note % 12) / 12.0, 0.75, 0.85)
	var octave := note / 12 - 5
	return c.lightened(minf(0.18 * octave, 0.9)) if octave >= 0 else c.darkened(minf(-0.18 * octave, 0.9))

func _pulse(r: Control, size: float, seconds: float) -> void:
	r.pivot_offset = r.size / 2.0
	r.scale = Vector2(size, size)
	create_tween().tween_property(r, "scale", Vector2.ONE, seconds)

func _report() -> void:
	var a := cellar.get_snapshot()
	var b := hallway.get_snapshot()
	print("B %8.3f beats at %6.2f BPM | cellar %s | hallway %s" % [FeluccaConductor.playhead, FeluccaConductor.tempo,
		_steps(a), _steps(b)])

func _steps(s: FeluccaSnapshot) -> String:
	if not s.bound:
		return "unbound"
	return " ".join(Array(s.steps).map(func(step: int) -> String: return "%2d" % step))

func _unhandled_input(e: InputEvent) -> void:
	var key := e as InputEventKey
	if e.is_action_pressed("ui_accept"):
		get_tree().paused = not get_tree().paused
		queue_redraw()
	elif key and key.pressed and not key.echo and key.physical_keycode >= KEY_1 and key.physical_keycode <= KEY_6:
		_cellar_key(key.physical_keycode - KEY_1 + 1, key.shift_pressed)

# the number keys: schedules on the Cellar (with Shift, a mask over what each plays)
func _cellar_key(n: int, masked: bool) -> void:
	var drums := cellar.part_4
	var said := ""
	match n:
		1:
			for beat in 4:
				cellar.schedule(FeluccaWhen.next_bar(beat + 1), FeluccaAction.hit(drums, FeluccaPart.Lane.LANE_SNARE, 70 + beat * 15))
			if masked:
				cellar.schedule(FeluccaWhen.next_bar(), FeluccaAction.mask_bars(drums, 1))
			said = "snares on the next bar's four beats"
		2:
			var length := drums.seq_length
			var heard := cellar.get_snapshot()
			var this_loop := heard.bound and heard.steps[3] < length - 4   # (its first step still well ahead)
			for i in 4:
				var step := length - 3 + i
				var when := FeluccaWhen.next_step(drums, step) if this_loop else FeluccaWhen.next_loop(drums, step)
				cellar.schedule(when, FeluccaAction.hit(drums, FeluccaPart.Lane.LANE_SNARE, 70 + i * 15))
			if masked:
				var first := length - 3
				cellar.schedule(FeluccaWhen.next_step(drums, first) if this_loop else FeluccaWhen.next_loop(drums, first),
					FeluccaAction.mask_steps(drums, 4))
			said = "a fill on steps %d to %d of the drums' %s loop" % [length - 3, length, "current" if this_loop else "next"]
		3, 4:
			var part := cellar.part_3
			var root := randi_range(48, 60)
			var steps := clampi(roundi(1.0 / DIV_BEATS[part.seq_division]), 1, 64)   # (a beat long)
			for i in 3:
				var note := root + MAJOR[i]
				var action := FeluccaAction.note(part, note, 100, steps) if n == 3 else FeluccaAction.note_layered(part, note, 100, steps)
				cellar.schedule(FeluccaWhen.next_bar(i + 1), action)
				if masked:
					cellar.schedule(FeluccaWhen.next_bar(i + 1), FeluccaAction.mask_steps(part, steps))
			said = "%s %s %s on part 3, %s, on the next bar" % [_name(root), _name(root + 2), _name(root + 4),
				"in place of its own" if n == 3 else "over its own"]
		5:
			var part := cellar.part_1
			var note := randi_range(48, 60)
			var heard := cellar.get_snapshot()
			var step := (heard.steps[0] + 1) % part.seq_length + 1 if heard.bound else 1   # (two steps on from the one heard)
			cellar.schedule(FeluccaWhen.next_step(part, step), FeluccaAction.note(part, note))
			if masked:
				cellar.schedule(FeluccaWhen.next_step(part, step), FeluccaAction.mask_steps(part, 1))
			said = "%s on part 1, step %d" % [_name(note), step]
		6:
			var part := cellar.part_2
			var view := part.sample                   # (null unless part 2's engine is SAMPLE)
			if view == null:
				said = "part 2 does not play SAMPLE"
			else:
				var original := view.tune                 # (semitones, -24 .. 24)
				var way := 1 if original + 4 <= 24 else -1
				var heard := cellar.get_snapshot()
				var first := (heard.steps[1] + 1) % part.seq_length + 1 if heard.bound else 1   # (two steps on)
				for i in 5:                           # a semitone a step for four steps, the fifth back where it was
					var step := (first - 1 + i) % part.seq_length + 1
					var value := original + way * (i + 1) if i < 4 else original
					cellar.schedule(FeluccaWhen.next_step(part, step),
						FeluccaAction.set_param(part, FeluccaSample.Param.PARAM_TUNE, value))
				said = "part 2 tuned %+d to %+d a semitone a step (from step %d), back to %+d on the fifth" % [original + way,
					original + 4 * way, first, original]
			masked = false                            # (nothing to mask)
	said = "Cellar: " + said + (" (masked)" if masked else "")
	print(said)
	hud.text = HELP + "\n" + said

func _name(note: int) -> String:
	return "%s%d" % [NAMES[note % 12], note / 12 - 1]

func _process(_delta: float) -> void:
	queue_redraw()

# the listener, as Godot's 2D players find it without an AudioListener2D: the screen's centre (the camera)
func _ear() -> Vector2:
	var vp := get_viewport()
	return (vp.global_canvas_transform * vp.canvas_transform).affine_inverse() * (vp.get_visible_rect().size * 0.5)

func _in_range(s: FeluccaSource2D, at: Vector2) -> bool:
	return s.playing and not s.stream_paused and s.global_position.distance_to(at) <= s.max_distance

func _draw():
	var at := _ear()
	for s in sources:
		var c := Color(0.25, 0.25, 0.25)
		if s.is_bound():
			c = Color.LIME_GREEN
		elif _in_range(s, at):
			c = Color.ORANGE
		elif s.playing:
			c = Color.GRAY
		draw_arc(s.position, s.max_distance, 0.0, TAU, 128, Color(c, 0.25), 2.0)
		draw_arc(s.position, 30.0, 0.0, TAU, 32, c, 4.0)
		draw_string(ThemeDB.fallback_font, s.position + Vector2(38, 8), s.name, HORIZONTAL_ALIGNMENT_LEFT, -1, 18, c)
	if get_tree().paused:
		draw_string(
			ThemeDB.fallback_font,
			Vector2(20, 40),
			"Paused",
			HORIZONTAL_ALIGNMENT_LEFT,
			-1,
			24,
			Color.YELLOW
		)
