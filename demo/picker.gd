# The demos' first scene (project.godot's main scene, the web's too): the demos, picked with a click or a number key.
# In a browser that click or key is also the gesture a page waits for before it may play sound, so each demo's music
# starts with it. P or Escape in a demo picked here comes back. (In the "Web tests" export it gives way to the tests'
# launcher, whose feature's main scene this one's may override.)
extends Control

const NAMES: PackedStringArray = ["main", "pool", "knock", "3d"]
const SCENES: PackedStringArray = ["res://main.tscn", "res://pool.tscn", "res://knock.tscn", "res://3d/main3d.tscn"]

@onready var list: VBoxContainer = %List

func _ready() -> void:
	if OS.has_feature("web_tests"):
		get_tree().change_scene_to_file.call_deferred("res://tests/web_tests.tscn")
		return
	for i in NAMES.size():
		var b := Button.new()
		b.text = "%d  %s" % [i + 1, NAMES[i]]
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.pressed.connect(_open.bind(i))
		list.add_child(b)
	(list.get_child(0) as Button).grab_focus()

func _unhandled_input(e: InputEvent) -> void:
	var key := e as InputEventKey
	if key and key.pressed and not key.echo and key.physical_keycode >= KEY_1 and key.physical_keycode < KEY_1 + NAMES.size():
		_open(key.physical_keycode - KEY_1)

func _open(i: int) -> void:
	if not get_tree().root.has_node("PickerKeys"):
		var keys := PickerKeys.new()
		keys.name = "PickerKeys"
		get_tree().root.add_child(keys)
	get_tree().change_scene_to_file.call_deferred(SCENES[i])

# P or Escape: back to the picker. A node beside the current scene, as an autoload is, so it outlives each scene
# change; made by the first pick, so a demo run on its own keeps its keys. It takes the key before the demo does
# (_input), works while the demo is paused, and undoes what a demo leaves: the pause, the 3D scene's captured mouse,
# the conductor running (each demo sets its tempo and plays it again).
class PickerKeys extends Node:
	const PICKER := "res://picker.tscn"

	func _ready() -> void:
		process_mode = Node.PROCESS_MODE_ALWAYS

	func _input(e: InputEvent) -> void:
		var key := e as InputEventKey
		if not key or not key.pressed or key.echo or (key.physical_keycode != KEY_P and key.physical_keycode != KEY_ESCAPE):
			return
		var tree := get_tree()
		if tree.current_scene and tree.current_scene.scene_file_path == PICKER:
			return
		get_viewport().set_input_as_handled()
		tree.paused = false
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		FeluccaConductor.stop()
		tree.change_scene_to_file.call_deferred(PICKER)
