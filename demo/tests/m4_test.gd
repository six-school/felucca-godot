# M4's checks in Godot 4.7.2 (extension/run_tests.sh): the typed API (FeluccaPart, its engine views, FeluccaSong,
# FeluccaOverrides), generated from the firmware's tables, on the user's project ("SOLAR SIGNAL": PHASE, PHASE, LOFI,
# DRUM; part 1 LEN 32). Values set unbound are applied at binding; bound, they reach the instance at once.
extends Node

var fails := 0
var project: FeluccaProject = preload("res://music/solar_signal.felucca")

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		fails += 1

func wait(seconds: float) -> void:
	await get_tree().create_timer(seconds, true, false, true).timeout

func mute_entry(part: int) -> FeluccaOverride:
	var m := FeluccaOverride.new()
	m.part = part
	m.param = FeluccaPart.Param.PARAM_MUTE
	m.value = 1
	return m

func source(overrides: FeluccaOverrides = null) -> FeluccaSource:
	var s := FeluccaSource.new()
	s.project = project
	s.overrides = overrides
	add_child(s)
	return s

func _ready() -> void:
	FeluccaConductor.tempo = project.bpm
	FeluccaConductor.boot_pool()
	FeluccaConductor.play()

	# ---- the classes, from the firmware's tables
	var props := ClassDB.class_get_property_list("FeluccaPart", true).filter(func(p: Dictionary) -> bool: return p.usage != 0)
	check(props.size() >= 100 and ClassDB.class_exists("FeluccaAnalog") and ClassDB.class_exists("FeluccaFm6")
		and not ClassDB.class_exists("FeluccaDigital"), "classes: FeluccaPart (%d properties), 13 engine views (no retired DIGITAL)" % props.size())
	check(FeluccaPart.ArpRate.ARP_RATE_1_16 == 2 and FeluccaPart.ArpRate.ARP_RATE_4BAR == 9 and FeluccaPart.Engine.ENGINE_ANALOG == 0
		and FeluccaPart.Engine.ENGINE_PHASE == 2 and FeluccaAnalog.Wave.WAVE_SIN == 3, "enums: the firmware's values (ARP RATE 1/16 = 2, PHASE = 2, ANALOG WAVE SIN = 3)")

	# ---- no method or property of ours shadows one of an engine ancestor (Node2D.position is get_position's)
	var clashes: Array[String] = []
	for c in ["FeluccaSource", "FeluccaSource2D", "FeluccaSource3D", "FeluccaPart", "FeluccaSong", "FeluccaAnalog",
			"FeluccaProject", "FeluccaOverride", "FeluccaOverrides", "FeluccaSnapshot", "FeluccaConductorNode"]:
		var base := ClassDB.get_parent_class(c)
		while base.begins_with("Felucca"):
			base = ClassDB.get_parent_class(base)
		for m: Dictionary in ClassDB.class_get_method_list(c, true):
			if ClassDB.class_has_method(base, m.name):
				clashes.append("%s.%s()" % [c, m.name])
		for q: Dictionary in ClassDB.class_get_property_list(c, true):
			if q.usage & PROPERTY_USAGE_CATEGORY == 0 and q.name in ClassDB.class_get_property_list(base).map(func(x: Dictionary) -> String: return x.name):
				clashes.append("%s.%s" % [c, q.name])
	check(clashes.is_empty(), "no name of ours shadows an engine ancestor's %s" % [clashes])

	# ---- unbound: the project's values; what is set is kept
	var a := source()
	var p1: FeluccaPart = a.part_1
	check(p1.engine == FeluccaPart.Engine.ENGINE_PHASE and p1.seq_length == 32 and a.part_4.seq_length == 16 and a.part_4.engine == FeluccaPart.Engine.ENGINE_DRUM,
		"unbound: the project's values (part 1 PHASE, LEN %d; part 4 DRUM, LEN %d)" % [p1.seq_length, a.part_4.seq_length])
	check(p1.analog == null and p1.phase != null and a.part_3.lofi != null, "engine views: the part's engine's only")
	p1.arp_rate = FeluccaPart.ArpRate.ARP_RATE_1_16
	p1.arp_swing = 500
	a.song.delay_mix = 17
	check(p1.arp_rate == FeluccaPart.ArpRate.ARP_RATE_1_16 and p1.arp_swing == 100 and a.song.delay_mix == 17,
		"unbound: set, kept, clamped (ARP SWG 500 -> %d)" % p1.arp_swing)
	check(p1.get("arp_2_swg") == 100, "the generated name stays as a fallback (arp_2_swg = arp_swing)")
	a.part_2.engine = FeluccaPart.Engine.ENGINE_ANALOG
	var cut0: int = a.part_2.analog.cutoff if a.part_2.analog else -1
	check(a.part_2.analog != null and a.part_2.phase == null and cut0 >= 0,
		"unbound engine change: the ANALOG view, its CUT by default %d" % cut0)
	a.part_2.analog.cutoff = 33

	# ---- bound: applied at binding, then live
	a.play()
	await wait(0.2)
	check(a.is_bound() and p1.arp_rate == FeluccaPart.ArpRate.ARP_RATE_1_16 and p1.arp_swing == 100 and a.song.delay_mix == 17
		and a.part_2.engine == FeluccaPart.Engine.ENGINE_ANALOG and a.part_2.analog.cutoff == 33 and p1.seq_length == 32,
		"binding: what was set is the instance's (ARP RATE %d, SWG %d, delay MIX %d, part 2 ANALOG CUT %d), the rest the project's"
		% [p1.arp_rate, p1.arp_swing, a.song.delay_mix, a.part_2.analog.cutoff if a.part_2.analog else -1])
	a.part_4.level = 50
	a.part_2.analog.cutoff = 500
	check(a.part_4.level == 50 and a.part_2.analog.cutoff == 127, "live: set on the instance at once, clamped by its descriptor")
	a.part_3.engine = FeluccaPart.Engine.ENGINE_ANALOG
	await wait(0.1)
	check(a.part_3.analog != null and a.part_3.lofi == null and a.part_3.analog.cutoff == cut0,
		"live engine change: LOFI -> ANALOG, its first preset (CUT %d, as unbound said)" % [a.part_3.analog.cutoff if a.part_3.analog else -1])
	a.part_3.analog.resonance = 90
	check(a.part_3.analog.resonance == 90, "an engine view's property (ANALOG RES via its alias resonance)")
	a.part_3.engine = FeluccaPart.Engine.ENGINE_LOFI
	check(a.part_3.analog == null, "the view goes with its engine")

	# ---- overrides: at binding, over the project's
	var o := FeluccaOverrides.new()
	var e := FeluccaOverride.new()
	e.part = 1
	e.param = FeluccaPart.Param.PARAM_SEQ_LENGTH
	e.value = 8
	var m4 := mute_entry(4)
	o.entries = [e, m4]
	var b := source(o)
	check(b.part_1.seq_length == 8 and b.part_4.mute == 1 and b.part_2.seq_length == 16,
		"overrides, unbound: part 1 LEN %d, part 4 muted %d" % [b.part_1.seq_length, b.part_4.mute])
	b.play()
	await wait(0.2)
	check(b.is_bound() and b.part_1.seq_length == 8 and b.part_4.mute == 1, "overrides, bound: the instance's (LEN %d, MUTE %d)"
		% [b.part_1.seq_length, b.part_4.mute])

	# ---- overrides are live: a change reaches the bound instance at once, never over what a script set
	var plain := source(null)               # (the project's own values, read unbound)
	var len1 := plain.part_1.seq_length
	var mutes := plain.parts.map(func(p: FeluccaPart) -> int: return p.mute)
	m4.part = 1
	check(b.part_1.mute == 1 and b.part_4.mute == mutes[3], "an entry moved to another part, bound: part 1 muted, part 4 back to the project's (%d, %d)"
		% [b.part_1.mute, b.part_4.mute])
	e.value = 12
	check(b.part_1.seq_length == 12, "an entry's value changed, bound: LEN %d" % b.part_1.seq_length)
	o.entries = [m4]
	check(b.part_1.seq_length == len1, "the entry removed: back to the project's (LEN %d, the project's %d)" % [b.part_1.seq_length, len1])
	b.part_2.mute = 0
	o.entries = [m4, mute_entry(2)]
	check(b.part_2.mute == 0 and b.part_1.mute == 1, "what a script set wins over a change of the overrides (part 2 MUTE %d)" % b.part_2.mute)
	var o2 := FeluccaOverrides.new()
	o2.entries = [mute_entry(3)]
	b.overrides = o2
	check(b.part_3.mute == 1 and b.part_1.mute == mutes[0], "another overrides resource, bound: its values at once, the old one's undone (part 3 %d, part 1 %d)"
		% [b.part_3.mute, b.part_1.mute])
	o2.entries = []
	o.entries = [mute_entry(3)]
	check(b.part_3.mute == mutes[2], "the old resource no longer heard (part 3 %d)" % b.part_3.mute)
	plain.overrides = o2
	o2.entries = [mute_entry(4)]
	check(plain.part_4.mute == 1, "unbound: the reads follow the overrides (part 4 %d)" % plain.part_4.mute)

	# ---- the source's own muted_parts: OR'd over each part's mute, live
	b.muted_parts = 0b0100
	check(b.part_3.mute == 1 and b.muted_parts == 0b0100, "muted_parts, bound: part 3 muted at once")
	b.part_3.mute = 0
	check(b.part_3.mute == 1, "a part's own mute set to 0 meanwhile: still muted (muted_parts holds it)")
	b.muted_parts = 0
	check(b.part_3.mute == 0, "muted_parts cleared: the part's own mute again (%d)" % b.part_3.mute)
	plain.muted_parts = 0b0010
	check(plain.part_2.mute == 1 and plain.part_1.mute == mutes[0], "muted_parts, unbound: read as muted (part 2 %d)" % plain.part_2.mute)
	plain.play()
	await wait(0.2)
	check(plain.is_bound() and plain.part_2.mute == 1 and plain.part_4.mute == 1, ".. and so at binding (part 2 %d, part 4 by its overrides %d)"
		% [plain.part_2.mute, plain.part_4.mute])
	plain.stop()

	# ---- a new binding gets what was set
	a.stop()
	await wait(0.3)
	check(not a.is_bound() and p1.arp_rate == FeluccaPart.ArpRate.ARP_RATE_1_16 and a.part_4.level == 50, "stopped: the values stay on the source")
	a.play()
	await wait(0.2)
	check(a.is_bound() and p1.arp_rate == FeluccaPart.ArpRate.ARP_RATE_1_16 and a.part_4.level == 50 and a.part_2.analog != null
		and a.part_2.analog.cutoff == 127, "played again, another instance: the same values (ARP RATE, LVL, part 2 ANALOG CUT 127)")

	print("m4_test: %d failed" % fails)
	a.stop()
	b.stop()
	await wait(0.2)
	get_tree().quit(1 if fails else 0)
