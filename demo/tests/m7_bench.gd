# Benchmarks, on the desktop and in a browser (extension/run_web_bench.sh): the pool's boot
# when the game starts; Felucca's heavy test song (heavy.felucca, as core/tests/test_heavy.c makes it) in 1, then
# 8 instances: the share of the audio thread their mixes take, and whether the mix keeps up with real time; the output
# latency the audio driver reports (the conductor's heard time adds it), and in a browser what an AudioContext of
# its own says. It measures; it checks only that every instance is bound, and in a browser that the mix keeps up and
# that there is a latency figure.
extends Node

const SECONDS := 10.0
const STEP_FRAMES := 512           # (Godot's mix step)
var project: FeluccaProject = preload("res://tests/heavy.felucca")
var fails := 0

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		fails += 1

func wait(seconds: float) -> void:
	await get_tree().create_timer(seconds, true, false, true).timeout

func bound(a: Array[FeluccaSource]) -> void:
	var t0 := Time.get_ticks_msec()
	while a.any(func(s: FeluccaSource) -> bool: return not s.is_bound()) and Time.get_ticks_msec() - t0 < 2000:
		await get_tree().process_frame

func _ready() -> void:
	var rate := AudioServer.get_mix_rate()
	var web := OS.has_feature("web")
	print("bench: %s, mix rate %d Hz" % ["web" if web else OS.get_name(), rate])
	var n := FeluccaConductor._get_booted_count()
	var boot := FeluccaConductor._get_boot_usec() / 1000.0
	print("bench boot: %d copies in %.1f ms when the game started (%.2f ms each)" % [n, boot, boot / maxi(n, 1)])
	if web:   # (the browser's own figures, from a context of its own: Godot's is not reachable from the page)
		JavaScriptBridge.eval("(() => { window.benchLatency = ''; const c = new AudioContext(); c.resume().then(() =>"
				+ " setTimeout(() => { window.benchLatency = [c.sampleRate, c.baseLatency, c.outputLatency].join(' ');"
				+ " c.close(); }, 500)); })()")
	FeluccaConductor.play()
	var sources: Array[FeluccaSource] = []
	for count in [1, 8]:
		while sources.size() < count:
			var s := FeluccaSource.new()
			s.project = project
			s.quantize = FeluccaSource.QUANTIZE_NOW
			add_child(s)
			s.play()
			sources.append(s)
		await bound(sources)
		await wait(1.0)
		var m0 := FeluccaConductor._get_mix_usec()
		var k0 := FeluccaConductor._get_mix_steps()
		var t0 := Time.get_ticks_usec()
		await wait(SECONDS)
		var mixed := (FeluccaConductor._get_mix_usec() - m0) / 1e6
		var steps := FeluccaConductor._get_mix_steps() - k0
		var wall := (Time.get_ticks_usec() - t0) / 1e6
		var audio := steps * STEP_FRAMES / rate
		var share := mixed / wall * 100.0
		print("bench %d: %.1f %% of the audio thread (%.2f %% an instance); %.3f ms a mix step of %.2f ms; %.2f s mixed in %.2f s"
				% [count, share, share / count, mixed / maxi(steps, 1) * 1000.0, STEP_FRAMES / rate * 1000.0, audio, wall])
		# (the browser's audio pulls in real time; the desktop's Dummy driver sleeps between steps, slower than that)
		check(sources.all(func(s: FeluccaSource) -> bool: return s.is_bound()) and (audio > wall * 0.98 or not web),
				"%d heavy: every one bound%s" % [count, ", the mix keeps up with real time" if web else ""])
	var latency := AudioServer.get_output_latency()
	print("bench latency: %.1f ms (AudioServer.get_output_latency)" % (latency * 1000.0))
	if web:
		var js := ""
		var t0 := Time.get_ticks_msec()
		while js == "" and Time.get_ticks_msec() - t0 < 3000:
			js = str(JavaScriptBridge.eval("window.benchLatency"))
			await get_tree().process_frame
		var f := js.split(" ")
		if f.size() == 3:
			print("bench latency: the browser's own AudioContext at %s Hz: base %.1f ms, output %.1f ms"
					% [f[0], float(f[1]) * 1000.0, float(f[2]) * 1000.0])
	check(latency > 0.0 or not web, "a latency figure for heard time")
	print("m7_bench: %d failed" % fails)
	get_tree().quit(fails)
