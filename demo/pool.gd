# The pool to try: seventeen sources of range 600 px (the demo's four projects), twelve playing from the start, in three
# groups; the pool has 8 engines, so at most 8 sources are heard at once. Arrow keys move the listener (the camera, as in
# main.tscn); 1, 2, 3 teleport it to the west, middle and east groups; C starts or stops five more sources around the
# east one (then 9 are in range there: the farthest waits); Space pauses. A source is green while it has an engine,
# orange while it is in range without one, grey while it is out of range without one, dark when stopped.
extends Node2D

const SPOTS: Array[Vector2] = [Vector2(0, 0), Vector2(1200, 0), Vector2(2400, 0)]

@onready var listener: Camera2D = $Listener
@onready var hud: Label = $Hud/Label
var sources: Array[FeluccaSource2D] = []
var crowd: Array[FeluccaSource2D] = []

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	for c in get_children():
		if c is FeluccaSource2D:
			sources.append(c)
			if c.name.begins_with("Crowd"):
				crowd.append(c)
	FeluccaConductor.tempo = sources[0].project.bpm
	FeluccaConductor.play()

func _unhandled_input(e: InputEvent) -> void:
	var key := e as InputEventKey
	if e.is_action_pressed("ui_accept"):
		get_tree().paused = not get_tree().paused
	elif key and key.pressed and not key.echo:
		match key.keycode:
			KEY_1, KEY_2, KEY_3:
				listener.position = SPOTS[key.keycode - KEY_1]
			KEY_C:
				var start := not crowd[0].playing
				for s in crowd:
					if start:
						s.play()
					else:
						s.stop()

# the listener, as Godot's 2D players find it without an AudioListener2D: the screen's centre (the camera)
func ear() -> Vector2:
	var vp := get_viewport()
	return (vp.global_canvas_transform * vp.canvas_transform).affine_inverse() * (vp.get_visible_rect().size * 0.5)

func in_range(s: FeluccaSource2D, at: Vector2) -> bool:
	return s.playing and not s.stream_paused and s.global_position.distance_to(at) <= s.max_distance

func _process(_delta: float) -> void:
	var at := ear()
	var near := sources.filter(func(s: FeluccaSource2D) -> bool: return in_range(s, at))
	var waiting := near.filter(func(s: FeluccaSource2D) -> bool: return not s.is_bound())
	hud.text = "%sin range: %d   engines in use: %d of %d   in range without one: %s\n" % [
		"PAUSED   " if get_tree().paused else "", near.size(), FeluccaConductor.get_bound_count(),
		FeluccaConductor.pool_size, ", ".join(PackedStringArray(waiting.map(func(s: Node) -> String: return s.name))) if waiting else "none"]
	hud.text += "arrows: move   1 2 3: teleport   C: five more sources in the east, on / off   Space: pause"
	queue_redraw()

func _draw() -> void:
	var at := ear()
	for s in sources:
		var c := Color(0.25, 0.25, 0.25)
		if s.is_bound():
			c = Color.LIME_GREEN
		elif in_range(s, at):
			c = Color.ORANGE
		elif s.playing:
			c = Color.GRAY
		draw_arc(s.position, s.max_distance, 0.0, TAU, 96, Color(c, 0.2), 2.0)
		draw_circle(s.position, 24.0, c)
		draw_string(ThemeDB.fallback_font, s.position + Vector2(30, 10), s.name, HORIZONTAL_ALIGNMENT_LEFT, -1, 28, c)
