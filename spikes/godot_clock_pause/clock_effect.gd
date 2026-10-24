# The conductor's per-mix-step hook: an effect that asks to run even when its bus is silent.
class_name ClockEffect extends AudioEffect
var steps := 0                     # mix steps seen (audio thread writes, main thread reads)
var paused_steps := 0
var paused := false                # published by the conductor under AudioServer.lock()
func _instantiate() -> AudioEffectInstance:
	var i := ClockInstance.new()
	i.fx = self
	return i
