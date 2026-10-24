# Autoload: first child of the root, so it hears NOTIFICATION_PAUSED before any scene node.
extends Node
var fx := ClockEffect.new()
var players: Array[AudioStreamPlayer] = []
var atomic := false                # true: pause/unpause every registered player at once, under the audio lock
var order: Array[String] = []      # who heard PAUSED first
func _ready() -> void:
	process_mode = Node.PROCESS_MODE_PAUSABLE
	AudioServer.add_bus()
	var b := AudioServer.bus_count - 1
	AudioServer.set_bus_name(b, "FeluccaClock")
	AudioServer.set_bus_mute(b, true)
	AudioServer.add_bus_effect(b, fx)
func _notification(what: int) -> void:
	if what == NOTIFICATION_PAUSED or what == NOTIFICATION_UNPAUSED:
		order.append("conductor")
		var p := what == NOTIFICATION_PAUSED
		AudioServer.lock()
		fx.paused = p
		if atomic:
			for pl in players:
				pl.stream_paused = p
		AudioServer.unlock()
