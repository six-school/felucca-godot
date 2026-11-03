# the typed API with one typo (bad_view.gd): a parse error
extends SceneTree
func _init() -> void:
	var s := FeluccaSource.new()
	var bass: FeluccaPart = s.part_1
	bass.arp_rate = FeluccaDivision.R_1_16
	bass.engine = FeluccaEngine.ANALOG
	var synth := bass.analog
	if synth:
		synth.cutof = 100
		synth.wave = FeluccaAnalog.Wave.WAVE_SAW
	s.song.delay_mix = 40
	s.part_2.arp_swing = 10
	quit()
