# The demo: the user's own project ("SOLAR SIGNAL", made on an FM-1, Felucca 1.6.1) in two sources, the cellar on the
# left and the hallway on the right, at the project's own tempo; the tempo eases to 80 BPM and back. Space pauses the
# game (and all of its music, at once); headless, it prints where both sources are.
extends Node2D

@onready var cellar: FeluccaSource2D = $Cellar
@onready var hallway: FeluccaSource2D = $Hallway

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var project := cellar.project
	print("project \"%s\" (%s), %d BPM: %s" % [project.project_name, project.firmware, project.bpm, ", ".join(project.engines)])
	FeluccaConductor.tempo = project.bpm
	FeluccaConductor.boot_pool()
	FeluccaConductor.play()
	cellar.play()
	hallway.play()
	var t := create_tween().set_loops()
	t.tween_property(FeluccaConductor, "tempo", 80.0, 8.0).set_delay(4.0)
	t.tween_property(FeluccaConductor, "tempo", float(project.bpm), 8.0).set_delay(4.0)
	var timer := Timer.new()
	timer.wait_time = 1.0
	timer.autostart = true
	timer.timeout.connect(_report)
	add_child(timer)

func _report() -> void:
	var a := cellar.get_position()
	var b := hallway.get_position()
	print("B %8.3f beats at %6.2f BPM | cellar %s | hallway %s" % [FeluccaConductor.beats, FeluccaConductor.tempo,
		_steps(a), _steps(b)])

func _steps(p: Dictionary) -> String:
	if not p.get("bound", false):
		return "unbound"
	return " ".join(p.tracks.map(func(t: Array) -> String: return "%2d" % (t[0] + 1)))

func _unhandled_input(e: InputEvent) -> void:
	if e.is_action_pressed("ui_accept"):
		get_tree().paused = not get_tree().paused
		queue_redraw()
		
func _draw():
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
