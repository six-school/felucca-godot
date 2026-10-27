# The knock and the door (DESIGN.md §1's scenario, M6's demo: listened to, not an automated test). Behind a door a room
# plays test-4 ("BLUE DRIFT"), its two synth parts' filters low, as heard through a door; its drums are 16 steps of
# 1/16, a bar, four to a beat. K knocks: on the drums' next loop their own hits are masked and four snares play on its
# beats (steps 1 5 9 13), dust flying off the door on each as it is heard (each snare's callback); one loop later, on
# that loop's first step, the synths' cutoff lifts (set_param, exact) and sweeps open over a bar (a tween) as the door
# swings open. F knocks fast: a snare on each of the next beat's four steps (1-4, 5-8, 9-12 or 13-16), their own hits
# masked, the door opening on the beat after. R opens the door at once, or closes it. Space pauses (the music and the
# door's animations with it).
extends Node2D

enum State { SHUT, KNOCKING, OPEN }
const MUFFLED := 25                 # the synths' cutoff behind the door
const LIFTED := 45                  # .. on the door's step
const OPENED := 110                 # .. once swept open

@onready var room: FeluccaSource2D = $Room
@onready var door: ColorRect = $Door
@onready var dust: CPUParticles2D = $Dust
@onready var hud: Label = $Hud/Label
var state := State.SHUT
var knocking := ""                  # (when, for the HUD)
var door_x := 0.0
var sweep: Tween

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS     # (Space unpauses)
	FeluccaConductor.tempo = room.project.bpm
	FeluccaConductor.play()
	for synth in _synths():
		synth.cutoff = MUFFLED
	room.play()
	door_x = door.position.x
	door.pivot_offset = Vector2(0, door.size.y / 2)   # (its hinge)
	_say()

func _unhandled_input(e: InputEvent) -> void:
	var key := e as InputEventKey
	if e.is_action_pressed("ui_accept"):
		get_tree().paused = not get_tree().paused
		_say()
	elif key and key.pressed and not key.echo and not get_tree().paused:
		if key.physical_keycode == KEY_K and state == State.SHUT:
			knock()
		elif key.physical_keycode == KEY_F and state == State.SHUT:
			knock_fast()
		elif key.physical_keycode == KEY_R and state == State.SHUT:
			_open()
		elif key.physical_keycode == KEY_R and state == State.OPEN:
			close()

# the drums' next loop masked, four snares on its beats; the door one loop later
func knock() -> void:
	var drums := room.part_4
	var hits: Array[FeluccaWhen] = []
	for step: int in [1, 5, 9, 13]:
		hits.append(FeluccaWhen.next_loop(drums, step))
	_knock("on the drums' next loop", hits, FeluccaAction.mask_loops(drums, 1), FeluccaWhen.loops_ahead(drums, 2, 1))

# the next beat's four steps masked, a snare on each; the door on the beat after
func knock_fast() -> void:
	var drums := room.part_4
	var heard := room.get_snapshot()
	var step := heard.steps[3] if heard.bound else 1   # (the drums' step heard now)
	var ahead := 4 - (step - 1) % 4                     # (to the next beat's first step)
	if ahead == 1 and heard.progress[3] > 0.5:
		ahead += 4      # (that step may be mixed already, ahead of where it is heard, its snare a loop late: the beat after)
	var hits: Array[FeluccaWhen] = []
	for k in 5:                                         # four snares, then the door
		hits.append(FeluccaWhen.next_step(drums, (step - 1 + ahead + k) % drums.seq_length + 1))
	var door_at: FeluccaWhen = hits.pop_back()
	_knock("on the next beat", hits, FeluccaAction.mask_steps(drums, 4), door_at)

# a snare on each of hits, the mask from the first; on door_at the cutoff lifts and the door opens
func _knock(what: String, hits: Array[FeluccaWhen], mask: FeluccaAction, door_at: FeluccaWhen) -> void:
	state = State.KNOCKING
	knocking = what
	_say()
	var drums := room.part_4
	room.schedule(hits[0], mask)
	for when in hits:
		room.schedule(when, FeluccaAction.hit(drums, FeluccaPart.Lane.LANE_SNARE, 120),
			func(_e: FeluccaEvent) -> void: _knocked())
	for part: FeluccaPart in [room.part_1, room.part_2]:   # (the lift lands on the door's step exactly)
		if part.engine == FeluccaPart.Engine.ENGINE_ANALOG:
			room.schedule(door_at, FeluccaAction.set_param(part, FeluccaAnalog.Param.PARAM_CUTOFF, LIFTED))
	await room.schedule(door_at, FeluccaAction.mark()).triggered
	_open()

# a snare, as heard: dust off the door, and it jolts
func _knocked() -> void:
	dust.restart()
	door.position.x = door_x + 5
	_tween().tween_property(door, "position:x", door_x, 0.15).set_trans(Tween.TRANS_ELASTIC)

func _open() -> void:
	state = State.OPEN
	_tween().tween_property(door, "scale:x", 0.12, 0.5).set_trans(Tween.TRANS_SINE)
	var bar := 4.0 * 60.0 / FeluccaConductor.tempo     # (a loop of the drums)
	sweep = _tween().set_parallel()
	for synth in _synths():
		sweep.tween_method(func(v: int) -> void: synth.cutoff = v, LIFTED, OPENED, bar)
	_say()

func close() -> void:
	state = State.SHUT
	if sweep:
		sweep.kill()
	_tween().tween_property(door, "scale:x", 1.0, 0.3).set_trans(Tween.TRANS_SINE)
	for synth in _synths():
		synth.cutoff = MUFFLED
	_say()

# the room's synth parts' ANALOG views (test-4's parts 1 and 2)
func _synths() -> Array[FeluccaAnalog]:
	var out: Array[FeluccaAnalog] = []
	for part: FeluccaPart in [room.part_1, room.part_2]:
		if part.analog:
			out.append(part.analog)
	return out

func _tween() -> Tween:
	return create_tween().set_pause_mode(Tween.TWEEN_PAUSE_STOP)   # (it stands with the music)

func _say() -> void:
	var what: String = ["The door is shut.", "Knocking: %s." % knocking, "The door is open."][state]
	hud.text = "K knock   F knock fast   R open or close the door   Space pause\n" + what + ("   (paused)" if get_tree().paused else "")
