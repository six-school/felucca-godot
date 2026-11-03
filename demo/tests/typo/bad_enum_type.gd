# the typed API with one typo (bad_enum_type.gd): another enum's value, a parse error
extends SceneTree
func _init() -> void:
	var s := FeluccaSource.new()
	var bass: FeluccaPart = s.part_1
	bass.arp_rate = FeluccaArpMode.UP
	bass.engine = FeluccaEngine.ANALOG
	var synth := bass.analog
	if synth:
		synth.cutoff = 100
		synth.wave = FeluccaAnalog.Wave.WAVE_SAW
	s.song.delay_mix = 40
	s.part_2.arp_swing = 10
	quit()
