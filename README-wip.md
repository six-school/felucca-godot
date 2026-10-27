Beyond here be slop docs

### Parameters

`part_1` .. `part_4` (also `parts`) are the project's four parts, `FeluccaPart`s; `song` (`FeluccaSong`) holds its
global settings. Each parameter of the device is a property of the part, and each engine's own are on the part's view
of that engine (`analog`, `drum`, `fm6`, ..: `null` while the part plays another engine). A value set before the source
has an instance is applied when it gets one.

```gdscript
var bass := cellar.part_1
bass.arp_rate = FeluccaPart.ArpRate.ARP_RATE_1_16
bass.send_reverb = 40
var synth := bass.analog    # FeluccaAnalog, or null
if synth:
  create_tween().tween_method(func(v: int) -> void: synth.cutoff = v, 30, 127, 0.8)
```

`engine` switches a part's engine (`FeluccaPart.Engine.ENGINE_ANALOG`, ..), loading its first preset as the device
does.

### Signals as heard

A part's signals come when the listener hears the sound, not when it is computed: `on_step(step, loop)`,
`on_loop(loop)`, `on_note(note, velocity)`, `on_hit(lane, velocity)` (a DRUM part) and `on_noteoff(note)`.

### Scheduling

`schedule(when, action, callback)` on a source holds an action until its moment in the music, and calls `callback`
when it is heard:

```gdscript
var drums := cellar.part_4
cellar.schedule(FeluccaWhen.next_loop(drums, 1), FeluccaAction.mask_loops(drums, 1))    # its own hits silent
for step in [1, 5, 9, 13]:
  cellar.schedule(FeluccaWhen.next_loop(drums, step), FeluccaAction.hit(drums, FeluccaPart.Lane.LANE_SNARE, 100))
cellar.schedule(FeluccaWhen.loops_ahead(drums, 2, 1), FeluccaAction.mark(), open_door)   # a loop later
```

- When: `FeluccaWhen.next_step(part, step)`, `next_loop(part, step)`, `loops_ahead(part, loops, step)`,
  `next_bar(beat)`. If the moment has passed by the time the source plays it: `LATE_NEXT` (the next one, the default),
  `LATE_DROP` or `LATE_NOW`.
- What: `FeluccaAction.hit(part, lane, velocity)` (in place of the step's own hit), `note(part, note, velocity, steps)`
  (in place of the step's notes), `note_layered(..)` (over them), `set_param(part, param, value)`,
  `mask_steps(part, n)`, `mask_loops(..)`, `mask_bars(..)` (the part's own steps silent), `mark()` (nothing sounds:
  only the callback).
- `schedule()` returns a `FeluccaSchedule`: its signal `triggered(event)`, `pending`, `dropped`, `cancel()`.
  `schedule_with_tag(when, action, tag)` and the source's `cancel(tag)` handle a group. A source with actions pending
  keeps its synth instance.