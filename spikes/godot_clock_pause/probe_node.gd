extends AudioStreamPlayer
func _notification(what: int) -> void:
	if (what == NOTIFICATION_PAUSED) and Conductor.order.size() < 3:
		Conductor.order.append(name)
