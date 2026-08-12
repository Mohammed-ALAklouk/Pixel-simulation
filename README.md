# Scree

*Scree — the sheet of loose rock fragments that mantles a slope.*

A falling-sand simulator written in C++20 with [raylib](https://github.com/raysan5/raylib).
Every pixel on the grid is an independent particle: sand piles up, water finds its
level, oil floats on top of it and burns, acid eats through stone, and lava that
touches water cools into fresh rock.

Materials are not hardcoded — they are defined in a JSON file that can be edited
and reloaded while the program is running.

<p align="center">
  <img src="docs/lava-water.gif" width="480"
       alt="Lava pouring off two shelves into water, boiling off steam and crusting over into hot stone">
</p>

## Demo

| Fire spreading through wood | Acid eating through stone |
|:---:|:---:|
| ![Fire spreading across a wooden figure, leaving ash and smoke behind](docs/fire-wood.gif) | ![Acid eating down through a stone slab and a wood layer, pooling on the sand below](docs/acid-stone.gif) |

## Features

- **640 × 640 particle grid**, updated at 60 FPS with a configurable number of
  simulation steps per frame.
- **Chunk-based updating.** The grid is split into 32 × 32 chunks. A chunk that
  had no activity last frame is skipped entirely, and the scan jumps a full chunk
  width ahead. Chunks wake their neighbours when something moves across a border,
  so a settled pile costs nothing until you disturb it.
- **Data-driven materials.** Movement (density, gravity, scatter), lifespan, tags,
  colour and every reaction come from
  [`Game/assets/materials.json`](Game/assets/materials.json). Adding a material — or
  a new interaction between materials — is a JSON edit, not a code change.
- **Live reload.** Edit the JSON, press *Reload material file*, and the change is
  in the running simulation. Blocks already on the grid are remapped by name, so
  reordering the file or inserting a material keeps existing pixels intact; anything
  whose name disappeared becomes air. A malformed material is reported in the UI and
  dropped, and the rest of the file still loads instead of crashing.
- **Density-based interaction.** Movement is decided by comparing densities rather
  than by special-casing pairs, so oil floats on water, water sinks through smoke,
  and anything heavier than a fluid sinks through it.
- **Data-driven reactions.** A material lists its reactions in JSON: a target (a
  specific material, or a tag like `flammable`), a chance, and weighted outcomes for
  the tile itself and for the neighbour it reacted with. Fire spreading to flammable
  neighbours, acid corroding into dirty water, and lava quenching into hot stone are
  all just JSON, not special cases in code.
- **Tags and anchoring.** Materials carry named tags with an intensity —
  `flammable`, `corrodible` — so one reaction can target a whole class of materials
  while each material sets its own susceptibility. A tag can also anchor: fire
  clings to flammable neighbours instead of drifting off before it has burnt them.
- **Lifespans.** A material can count down and transform when it expires, picking
  from a weighted list of outcomes — fire burning out into smoke or air, hot stone
  cooling back to stone. Colour can interpolate across the countdown, so fire fades
  as it dies.
- **Liquid surface skimming.** Liquids on an open surface scan ahead for the
  nearest drop-off instead of shuffling one cell per step, so pools level out
  quickly rather than creeping.
- **Randomised scan order.** Each row is swept left-to-right or right-to-left at
  random, and falling particles scatter, which keeps piles from developing a
  directional bias.
- **Debug overlays.** Toggle the active-chunk grid to see what the simulation is
  actually working on, and a benchmark window for per-stage frame timings.

## Materials

| Material | Behaviour |
|---|---|
| Air | Empty space. Anything denser moves straight through it. |
| Sand | Falls and cascades into slopes. Dissolves slowly in acid. |
| Stone | Static. The default wall material. |
| Wood | Static and flammable. Burns away, and corrodes fast in acid. |
| Water | Levels out, puts out fire, and quenches lava into hot stone. |
| Dirty Water | What acid leaves behind once it has eaten something. Lighter than water. |
| Oil | Floats on water and catches fire on contact. |
| Acid | Sinks through water, corrodes its neighbours, and turns to dirty water as it does. |
| Lava | Dense liquid. Ignites what it touches and crusts into hot stone in water. |
| Fire | Rises, spreads to flammable neighbours, and dies in water. |
| Smoke | Rises and fades out. Left behind by fire. |
| Steam | Rises and fades much faster. Boiled off water by hot stone. |
| Hot Stone | Freshly quenched lava. Cools into stone, boiling the water around it. |
| Ash | Falls like sand. What burnt wood leaves behind. |
| Hot Ash | Glowing ash from a fresh burn. Cools into ash. |

## Controls

| Input | Action |
|---|---|
| **Left mouse** | Draw the selected material |
| **Right mouse** | Erase |
| **Mouse wheel** | Resize the brush (radius 1–100) |
| **Material menu** | Pick a material, pause, clear, reload materials, toggle overlays |

Dragging draws a continuous line between frames, so fast strokes don't leave gaps.
Drawing only fills empty space — erase first to replace something. The mouse is
ignored by the canvas while it is over an ImGui window.

## How it works

The grid is stored as a flat array of `Chunk`s, each holding a 32 × 32 array of
`Block`s — an integer material id, a colour, and a lifespan counter. At 640 × 640
that is a 20 × 20 grid of chunks. Colours are randomised per particle within the
material's `color_min`/`color_max` range, quantised into `steps` bands, which gives
piles texture without storing anything extra.

Each frame runs two passes over the grid: one bottom-to-top for materials that fall,
one top-to-bottom for materials that rise, so gases and liquids can move past each
other in the same tick. Within a pass, each pixel resolves in order: lifespan tick →
reactions → movement (falling, cascading, liquid spreading). Lifespan runs before
reactions on purpose, so a reaction that reads a neighbour's remaining lifespan sees
this frame's value. A per-pixel processed flag stops a particle from being updated
twice after it has been swapped forward.

Chunk activity is tracked with two flags — active this frame and active next frame.
Any write marks the containing chunk for the next frame, and a write on a chunk edge
marks the neighbour too, which is what keeps interactions from stalling at chunk
boundaries.

## Adding a material

Materials live in the `materials` array of `Game/assets/materials.json`, and any
tags they use are declared once at the top of the file. A minimal entry:

```json
{
  "tags": [ "flammable", "corrodible" ],
  "materials": [
    {
      "name": "Sand",
      "interpolate_color": false,
      "color_min": [ 150, 150, 0 ],
      "color_max": [ 255, 255, 0 ],
      "steps": 8,
      "movement": {
        "y_direction": 1,
        "density": 80,
        "scatter_chance": 50,
        "can_fall": true,
        "can_cascade": true,
        "is_fluid": false,
        "is_liquid": false
      },
      "lifespan": { "initial": 255, "tick": 0, "on_death": [] },
      "tags": { "corrodible": 5 },
      "reactions": []
    }
  ]
}
```

Add your entry, then press *Reload material file*. Anything malformed is reported in
the UI and that material is dropped, but the rest of the file still loads.

**Top-level fields**

| Field | Meaning |
|---|---|
| `name` | Unique name; reactions and transitions reference materials by it |
| `color_min` / `color_max` | RGB range each particle's colour is picked from |
| `steps` | Number of discrete colour bands between min and max |
| `interpolate_color` | Colour tracks the lifespan instead: `color_min` when dead, `color_max` at full lifespan |

**`movement`**

| Field | Meaning |
|---|---|
| `y_direction` | `1` falls, `-1` rises (`0` is rejected and treated as `1`) |
| `density` | Decides what sinks through what; equal densities never displace each other |
| `scatter_chance` | Percent chance a falling particle drifts sideways |
| `can_fall` | Moves along the gravity direction |
| `can_cascade` | Slides diagonally, so it forms slopes instead of towers |
| `is_fluid` | Can be displaced by denser materials moving through it |
| `is_liquid` | Spreads sideways to level out |

**`lifespan`**

| Field | Meaning |
|---|---|
| `initial` | Starting counter, 0–255; also where interpolated colour begins |
| `tick` | Subtracted each update; `0` means it never dies |
| `on_death` | Weighted transitions applied when the counter reaches 0 (required once `tick` > 0) |

**`tags`** is an object mapping tag name → intensity (0–100). Intensity is the
percent chance that a reaction targeting this tag fires against the material. An
optional `"anchor": "<tag>"` makes the material stick to neighbours carrying that
tag instead of moving — this is what keeps fire on the wood it is burning.

**`reactions`** is an array; each reaction fires against the four orthogonal
neighbours:

| Field | Meaning |
|---|---|
| `target_type` | `"material"` or `"tag"` |
| `target` | Material name or tag name to react with |
| `chance` | Percent, material targets only; tag targets use the neighbour's tag intensity |
| `scan_sample` | `"all"` fires on every matching neighbour; `"first_to_react"` stops at the first |
| `halt_update` | After firing, skip this material's remaining reactions (movement still runs) |
| `self_transitions` | Weighted outcomes for the reacting tile |
| `target_transitions` | Weighted outcomes for the neighbour that was reacted with |

**Transitions** (each entry in `on_death`, `self_transitions`, or
`target_transitions`):

| Field | Meaning |
|---|---|
| `no_transition` | Leave the block unchanged; still needs a weight |
| `material` | Material to turn into |
| `weight` | Relative weight against the other outcomes in the same list |
| `lifespan_base` | Lifespan the new tile starts with: `initial` (new material's default), `self` (the reacting tile's old lifespan), or `reactor` (the neighbour's lifespan). `on_death` allows `initial` only. |

Because movement, lifespans and reactions are all data, a new material can be given
brand-new chemistry without touching the C++.

### Air

Air is built in. It always holds id 0 and is not listed in `materials.json` — it is
what an empty cell is, what a rejected material falls back to, and what a block turns
into when its material disappears on reload. A file entry named `Air` is rejected as
a duplicate, so it cannot be redefined; file materials start at id 1.

### Error reporting

Nothing is silently swallowed. Every problem the loader hits becomes a log line —
prefixed `[Warning]` or `[Error]`, naming the material and field — collected and
shown at the top of the material menu after a load, with a *Clear error message*
button. Warnings appear even on an otherwise successful load. Problems fall into
three tiers by how much they throw away:

- **Warning** — a value was clamped or ignored and the load continues. An
  out-of-range number is pulled into range (a `density` above 255, say), a float is
  truncated to an int, or a `lifespan_base` that is valid but meaningless in context
  is dropped.
- **Reject material** — that one entry is unusable, so it loads as an inert magenta
  placeholder while every other material still comes in. Triggered by a missing or
  non-string `name`, a duplicate name, a field of the wrong type, an unknown
  tag / material / `lifespan_base` reference, or exceeding the material or tag limit.
- **Reject file** — the whole load is abandoned and the previous materials stay in
  place. Only a missing file or JSON that does not parse gets this far.

The file's verdict is the worst tier any single log reached, so one reject-file line
sinks the load while a stack of warnings does not.

## Building

Requires **CMake 3.21+** and a **C++20** compiler. The `vs2026` preset additionally
needs a CMake new enough to know the `Visual Studio 18 2026` generator — check with
`cmake --help`. raylib, Dear ImGui, rlImGui and
nlohmann/json are pulled in automatically by `FetchContent` on first configure —
there is nothing to install by hand. They are checked out into `.deps/` at the
repository root rather than inside `build/`, so wiping a build directory does not
force a re-download.

`cmake --list-presets` shows the presets available on your platform.

**Windows (Visual Studio 2022)**

```bash
cmake --preset vs2022 && cmake --build --preset vs2022-release
```

**Windows (Visual Studio 2026)**

```bash
cmake --preset vs2026 && cmake --build --preset vs2026-release
```

**Any platform (Ninja)**

```bash
cmake --preset ninja && cmake --build --preset ninja-release
```

**Linux / macOS (Make, no Ninja required)**

```bash
cmake --preset make-release && cmake --build --preset make-release
```

Swap `release` for `debug` in any of the above. Each preset configures into its own
`build/<preset>/` directory, so the executable lands in
`build/vs2022/Game/Release/` (or `build/make-release/Game/` for the Make presets).

Because a CMake build directory is bound to the generator that created it, giving
every preset its own directory means they coexist -- switching between them costs
nothing and needs no cleanup. The downloaded dependencies live in `.deps/` at the
repository root and are shared across all of them, so switching never triggers a
re-download.

### Tests

A [Catch2](https://github.com/catchorg/Catch2) suite in `tests/` exercises the
material parser — schema validation, the weighted-transition maths, and a golden
parse of the shipped `materials.json`. It builds by default for a top-level build and
is skipped when Scree is consumed as a subproject, gated behind the `SCREE_BUILD_TESTS`
option. Catch2 is fetched into `.deps/` alongside the other dependencies on first
configure. Run it with:

```bash
ctest --test-dir build/<preset> --output-on-failure
```

Add `-C Release` (or `-C Debug`) for the Visual Studio presets, which are
multi-config.

### Linux system packages

raylib builds GLFW from source and needs the X11 and audio development headers:

```bash
sudo apt install libasound2-dev libx11-dev libxrandr-dev libxi-dev \
    libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev libxkbcommon-dev
```

On Fedora the equivalents are `alsa-lib-devel mesa-libGL-devel libX11-devel
libXrandr-devel libXi-devel libXcursor-devel libXinerama-devel libxkbcommon-devel`.
macOS needs only the Xcode command line tools; raylib links the system frameworks
itself.

### Assets

Assets are linked, not copied, so editing `Game/assets/materials.json` in the source
tree affects the built executable immediately with no rebuild — which is what makes
the in-app *Reload material file* button useful. The path is resolved relative to the
executable, so it works no matter what directory you launch from.

The link is made by a post-build step: a junction on Windows (`mklink /J`, which
unlike a real symlink never needs admin rights or Developer Mode), and
`cmake -E create_symlink` everywhere else.

## Roadmap

- A per-pixel heat field to replace the hot-stone / hot-ash lifespan hacks, with
  ice, glass and obsidian falling out of it
- Bitmap rendering and dirty-chunk uploads, then multithreaded chunk passes
- Brush shapes, a size slider, line and rectangle tools
- Save / load worlds and image import
- A downloadable build, and a web build via Emscripten

## License

[MIT](LICENSE)
