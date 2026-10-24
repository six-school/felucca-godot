extends Node
const N := 8
const FILLER := 4000               # plain nodes between players: pause notifications take a while to propagate
var players: Array[AudioStreamPlayer] = []
func _silence(seconds: float) -> AudioStreamWAV:
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_16_BITS
	w.mix_rate = 44100
	w.stereo = true
	var d := PackedByteArray()
	d.resize(int(seconds * 44100) * 4)
	w.data = d
	return w
func _spread() -> int:             # max - min mixed position over the players, in frames, read under the lock
	AudioServer.lock()
	var lo := INF
	var hi := -INF
	for p in players:
		var f := p.get_playback_position() * 44100.0
		lo = min(lo, f)
		hi = max(hi, f)
	AudioServer.unlock()
	return int(round(hi - lo))
func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS          # the test driver itself keeps running
	var wav := _silence(600.0)
	var probe := preload("res://probe_node.gd")
	for i in N:
		var holder := Node.new()
		holder.process_mode = Node.PROCESS_MODE_PAUSABLE
		add_child(holder)
		for k in FILLER:
			holder.add_child(Node.new())
		var p := AudioStreamPlayer.new()
		p.set_script(probe)
		p.name = "player%d" % i
		p.stream = wav
		holder.add_child(p)
		players.append(p)
	Conductor.players = players
	await get_tree().create_timer(0.3).timeout
	# Test 1: the clock effect runs once per mix step, nothing routed to its muted bus
	var s0 := Conductor.fx.steps
	await get_tree().create_timer(2.0).timeout
	print("T1 clock effect: %d steps in 2.0 s (512-frame steps at 44.1 kHz: %.0f expected)" % [Conductor.fx.steps - s0, 2.0 * 44100 / 512])
	get_tree().paused = true
	var p0 := Conductor.fx.paused_steps
	await get_tree().create_timer(1.0).timeout
	get_tree().paused = false
	print("T1 while the game was paused: the effect still ran (%d steps flagged paused in 1 s)" % (Conductor.fx.paused_steps - p0))
	print("T1 who heard PAUSED first: %s" % [Conductor.order.slice(0, 3)])
	# Test 2: pause/unpause the tree many times; do the players stay at the same mixed position?
	for mode in ["Godot default (per node)", "conductor, atomic under AudioServer.lock()"]:
		Conductor.atomic = mode.begins_with("conductor")
		for p in players:
			p.stop()
		AudioServer.lock()
		for p in players:
			p.play()
		AudioServer.unlock()
		await get_tree().create_timer(0.2).timeout
		var splits := 0
		var start := _spread()
		for k in 300:
			get_tree().paused = true
			await get_tree().create_timer(randf_range(0.0, 0.03), true, false, true).timeout
			get_tree().paused = false
			await get_tree().create_timer(randf_range(0.0, 0.03), true, false, true).timeout
			if _spread() > 0:
				splits += 1
		print("T2 %s: start spread %d frames; after 300 pause/unpause cycles: spread %d frames (%d checks out of step)" % [mode, start, _spread(), splits])
	get_tree().quit()
