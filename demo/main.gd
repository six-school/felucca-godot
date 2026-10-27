# The demo: the user's own project ("SOLAR SIGNAL", made on an FM-1, Felucca 1.6.1) in two sources, the cellar on the
# left and the hallway on the right, at the project's own tempo; the tempo eases to 80 BPM and back. Space pauses the
# game (and all of its music, at once); headless, it prints where both sources are. Each source is drawn with its range
# and a ring: green while it has an engine, orange in range without one, grey playing out of range, dark stopped; its
# squares pulse on each pass of its drum part (part 4's on_loop: on 16 steps of 1/16, a bar). Above it, a small square a
# part (1 to 4, left to right) pulses on each note the part starts (on_note; a DRUM part's on_hit), coloured by the
# note's place in the octave (C red, round the hues to B), lighter an octave up from middle C, darker an octave down,
# and greys once the part's notes are all released (on_noteoff).
extends Node2D

@onready var cellar: FeluccaSource2D = $Cellar
#@onready var upstairs: FeluccaSource2D = $Upstairs
@onready var hallway: FeluccaSource2D = $Hallway
var sources: Array[FeluccaSource2D] = []

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
	if e.is_action_pressed("ui_accept"):
		get_tree().paused = not get_tree().paused
		queue_redraw()

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
