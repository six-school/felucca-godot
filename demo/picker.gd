# The demos' first scene (project.godot's main scene, the web's too): the demos (scenes, each shown by its file's
# name), picked with a click or a number key. In a browser that click or key is also the gesture a page waits for
# before it may play sound, so each demo's music starts with it. P or Escape in a demo picked here comes back. (In the
# "Web tests" export it gives way to the tests' launcher, whose feature's main scene this one's may override.)
extends Control

@export var scenes: Array[PackedScene] = []

@onready var list: VBoxContainer = %List

func _ready() -> void:
	if OS.has_feature("web_tests"):
		get_tree().change_scene_to_file.call_deferred("res://tests/web_tests.tscn")
		return
	for i in scenes.size():
		var b := Button.new()
		b.text = "%d  %s" % [i + 1, scenes[i].resource_path.get_file().get_basename()]
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.pressed.connect(_open.bind(i))
		list.add_child(b)
	(list.get_child(0) as Button).grab_focus()

func _unhandled_input(e: InputEvent) -> void:
	var key := e as InputEventKey
	if key and key.pressed and not key.echo and key.physical_keycode >= KEY_1 and key.physical_keycode < KEY_1 + mini(scenes.size(), 9):
		_open(key.physical_keycode - KEY_1)

func _open(i: int) -> void:
	if not get_tree().root.has_node("PickerKeys"):
		var keys := PickerKeys.new()
		keys.name = "PickerKeys"
		keys.picker = scene_file_path
		get_tree().root.add_child(keys)
	get_tree().change_scene_to_packed.call_deferred(scenes[i])

# P or Escape: back to the picker. A node beside the current scene, as an autoload is, so it outlives each scene
# change; made by the first pick, so a demo run on its own keeps its keys. It takes the key before the demo does
# (_input), works while the demo is paused, and undoes what a demo leaves: the pause, the 3D scene's captured mouse,
# the conductor running (each demo sets its tempo and plays it again).
class PickerKeys extends Node:
	var picker := "" # (the picker's own scene)

	func _ready() -> void:
		process_mode = Node.PROCESS_MODE_ALWAYS

	func _input(e: InputEvent) -> void:
		var key := e as InputEventKey
		if not key or not key.pressed or key.echo or (key.physical_keycode != KEY_P and key.physical_keycode != KEY_ESCAPE):
			return
		var tree := get_tree()
		if tree.current_scene and tree.current_scene.scene_file_path == picker:
			return
		get_viewport().set_input_as_handled()
		tree.paused = false
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		FeluccaConductor.stop()
		tree.change_scene_to_file.call_deferred(picker)
