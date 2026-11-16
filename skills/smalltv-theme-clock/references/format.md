# theme.json: what the docs page does not say

The theme format is documented in one place, the mysmall-tv themes page:
[`docs/themes.md`](../../../docs/themes.md).
Read it for every layer type, field, token, binding and limit:

| Topic | Section |
|---|---|
| Root fields, IDs, `spec` | [Manifest](../../../docs/themes.md#манифест) |
| Text, tokens, anchors | [Text](../../../docs/themes.md#текст) |
| Per-language text (spec 2) | [Localized text](../../../docs/themes.md#локализованный-текст) |
| Scrolling | [Scrolling text](../../../docs/themes.md#прокрутка-текста) |
| Image, animation | [Image](../../../docs/themes.md#изображение), [Animation](../../../docs/themes.md#анимация) |
| Rectangle, circle, line | [Shape](../../../docs/themes.md#фигура) |
| Arc, hand, ticks (spec 2) | [Arcs, hands and tick marks](../../../docs/themes.md#дуги-стрелки-и-деления-spec-2) |
| `bind`, mapping, color stops, which property takes which | [Dynamic bindings](../../../docs/themes.md#динамические-привязки), [Property matrix](../../../docs/themes.md#таблица-свойств) |
| `time.*`, `device.*` (spec 2) | [Built-in sources](../../../docs/themes.md#встроенные-источники) |
| Fetched JSON | [Small JSON data sources](../../../docs/themes.md#небольшие-источники-данных-json) |
| Sizes, counts, storage | [Limits and storage](../../../docs/themes.md#ограничения-и-хранение) |

In a clone of the repository, read the markdown file directly. Without network or a clone,
rely on `stheme.py validate`: it is the source of truth for this skill (the repository's CI
checks it against the firmware validator), and its errors name the exact field. This file
only adds what an agent needs that the docs page does not cover.

## `"spec": "auto"`

A skill convention, not a firmware value. Write `"spec": "auto"` in the source and
`stheme.py build` resolves it to the lowest spec the manifest needs, then writes the number
back into `theme.json` (it says so in its output; `--keep-auto` leaves the file alone).
The firmware tools and SmallTV Studio only accept the number.

These force spec 2 (firmware 3.4.0+, not editable in SmallTV Studio yet):

- `arc`, `hand` and `ticks` shapes;
- `time.*` and `device.*` binding sources;
- the `{H}`, `{MO}`, `{AMPM}` and `{device.*}` text tokens;
- per-language text values (`{"en": …, "ru": …}`), and Russian letters in text.

Everything else builds as spec 1. Spec 1 always shows English month and day names; set
`"spec": 2` explicitly when the user wants them in the device language.

## Analog hand recipes (tested against the firmware renderer)

Angles are whole degrees, 0 at 12 o'clock, clockwise. Second and minute hands:

```json
{"id": "second", "type": "shape", "shape": "hand", "x": 120, "y": 120, "length": 95, "tail": 20, "angle": 0,
 "stroke": "#ff5a47", "strokeWidth": 1,
 "bind": {"angle": {"source": "time.second", "input": [0, 60], "output": [0, 360]}}}
{"id": "minute", "type": "shape", "shape": "hand", "x": 120, "y": 120, "length": 85, "tail": 14, "angle": 0,
 "stroke": "#e9edf4", "strokeWidth": 4,
 "bind": {"angle": {"source": "time.minute", "input": [0, 60], "output": [0, 360]}}}
```

The minute hand steps once a minute and the second hand once a second; there is no
sub-second source.

**Hour hand, stepping once an hour.** Binding outputs are capped at ±360, so one binding
cannot turn smoothly through 24 hours:

```json
"bind": {"angle": {"source": "time.hour12", "input": [0, 12], "output": [0, 360]}}
```

**Hour hand, smooth all day.** Two hands, one for AM and one for PM, each moved off-screen
(`x` bound to `time.hour`) during the other half of the day:

```json
{"id": "hour-am", "type": "shape", "shape": "hand", "x": 120, "y": 120, "length": 55, "tail": 10, "angle": 0,
 "stroke": "#e9edf4", "strokeWidth": 6,
 "bind": {"angle": {"source": "time.secondOfDay", "input": [0, 43200], "output": [0, 360]},
          "x": {"source": "time.hour", "input": [11, 12], "output": [120, 479]}}}
{"id": "hour-pm", "type": "shape", "shape": "hand", "x": -240, "y": 120, "length": 55, "tail": 10, "angle": 0,
 "stroke": "#e9edf4", "strokeWidth": 6,
 "bind": {"angle": {"source": "time.secondOfDay", "input": [43200, 86400], "output": [0, 360]},
          "x": {"source": "time.hour", "input": [11, 12], "output": [-240, 120]}}}
```

Before NTP sync there are no `time.*` values, so hands keep their static `angle` and `x`:
give the AM hand the on-screen static `x` (as above) so an unsynced dial still looks like a
clock. Draw the hands after the dial and a small center cap (circle) last.
