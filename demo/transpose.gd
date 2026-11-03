# A latched arpeggio transposed live: part 2 of the project is set up to arpeggiate (ARP on, HOLD on, its scale snapped),
# with no notes of its own; a chord scheduled once, one step long, latches there. Move the square with the arrow keys:
# where it is across the screen sets part 2's transpose, from -24 semitones at the left edge through 0 in the middle to
# +24 at the right, and the arpeggio follows as it plays, snapped to the part's scale as the FM-1's own keys are.
extends Node2D

const CHORD := "D4 F4 A4"
const RANGE := 24                       # semitones either way, from the middle of the screen to its edges

@onready var song: FeluccaSource = $Song
@onready var player: Node2D = $Player
@onready var hud: Label = $Hud/Label
var arp: FeluccaPart
var playing_note := ""

func _ready() -> void:
	arp = song.part_2
	arp.on_note.connect(func(note: int, _velocity: int) -> void: playing_note = FeluccaAction.int_to_note(note))
	player.position = get_viewport_rect().size * 0.5
	FeluccaConductor.tempo = song.project.bpm
	FeluccaConductor.play()
	song.play()
	song.schedule(FeluccaWhen.next_step(arp, 1), FeluccaAction.note(arp, CHORD))

func _process(_delta: float) -> void:
	var size := get_viewport_rect().size
	player.position = player.position.clamp(Vector2.ZERO, size)
	var t := clampi(roundi(remap(player.position.x, 0.0, size.x, -RANGE, RANGE)), -RANGE, RANGE)
	if arp.transpose != t:
		arp.transpose = t
	hud.text = "transpose %+d   playing %s\nleft and right: transpose part 2's arpeggio (%s, latched)" % [t, playing_note, CHORD]
	queue_redraw()

func _draw() -> void:
	var size := get_viewport_rect().size
	for k in range(-RANGE, RANGE + 1):  # a line a semitone, the octaves brighter
		var x := remap(k, -RANGE, RANGE, 0.0, size.x)
		draw_line(Vector2(x, 0), Vector2(x, size.y), Color(1, 1, 1, 0.3 if k % 12 == 0 else 0.08), 2.0 if k % 12 == 0 else 1.0)
