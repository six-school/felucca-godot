class_name ClockInstance extends AudioEffectInstance
var fx: ClockEffect
func _process_silence() -> bool:
	return true
func _process(_src, _dst, _frames: int) -> void:
	fx.steps += 1
	if fx.paused:
		fx.paused_steps += 1
