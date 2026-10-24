# the typed API, spelled right: parses (extension/run_tests.sh: --check-only)
extends SceneTree
func _init() -> void:
	var s := FeluccaSource.new()
	var bass: FeluccaPart = s.part_1
	bass.arp_rate = FeluccaPart.ArpRate.ARP_RATE_1_16
	bass.engine = FeluccaPart.Engine.ENGINE_ANALOG
	var synth := bass.analog
	if synth:
		synth.cutoff = 100
		synth.wave = FeluccaAnalog.Wave.WAVE_SAW
	s.song.delay_mix = 40
	s.part_2.arp_swing = 10
	quit()
