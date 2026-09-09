# GH3 PS2 source character conversion

This pipeline preserves the PS2 source skeleton, skinning and animation samples,
renames the target bone channels, derives GH2 arm frames, and retains source
instrument proxies. Both Midori outfits were reviewed in native gameplay and
approved for local DLC installation on 2026-09-08. The expanded
`midori_actions.json` recipe is the reusable native-game action conversion.

Requirements: Python 3.12, numpy, scipy, Pillow; the repository's
milo_convert_tool (build tools/milo_convert with CMake); a GH3 PS2 USA ISO;
and an external https://gitgud.io/fretworks/nxtools.git checkout. The source
proof used NXTools commit 6cea808. No game assets or NXTools are bundled here.
Run from the repository root:

```powershell
python tools/gh3_midori_ir.py --iso "path/to/Guitar Hero III - Legends of Rock (USA).iso" --nxtools "path/to/nxtools" --output "scratch/ir/midori_source_ir_manifest.json" --asset-dir "scratch/ir"
python tools/build_gh3_ps2_playing_probe.py --source "scratch/ir" --converter "path/to/milo_convert_tool.exe" --recipe tools/gh3_ps2_recipes/midori_actions.json --work "scratch/build" --output "output/community.gh3.midori"
```

Use empty work/output directories. `additional_outfits` builds the alternate
model and shares all four animation banks. The combined package contains two
models, four banks, a manifest and a hash index. The builder creates a candidate;
it does not install it. The older `midori_1.json`/`midori_2.json` recipes retain
the initial playing probes and do not provide the expanded action contract.

The recipes retain explicit source-to-instrument frame mappings and clip aliases.
Hand attacks use immediate playback and no body-style crossfade. Generic rigid
frame rebasing compensates descendants and checks world-pose invariance.
The milo builder's --preserve-guitar-proxies prevents replacement of source IK
helpers; generated root bone names also follow the normal GH2 channel suffix rule.

The expanded recipe supplies separate body, frontend, fret and strum banks.
Body clips retain source face/accessory overlays. Main selection flags preserve
source ownership of the arm chains: a complete authored arm is not overwritten
by hand IK. Source hand attacks keep their note-onset timing. Body and frontend
clips use source seconds; they are not assumed to have stock animation lengths.

| GH2 consumer | Converted source behavior |
|---|---|
| normal / idle / bad | Playing, break and poor-performance source actions |
| solo / extreme / star_power | Source solos and special performances |
| intro / win / lose / win_finals | Source intros and outcomes; campaign victory uses win3 |
| sync_jump | Source jump, then return to the current performance group |
| sync_wag / sync_head_bang | Existing source sway/head-nodding performances adapted to the group calls |
| walk_turn / walk_walk / walk_stop | Source strides/stops, extracted root motion and generated movement links |
| ui_enter / ui_loop | Frontend intro2, then its held terminal pose with complete face/hair cycles |
| 25 fret calls | Source finger-channel compositions, distinct high variants and source wrist bends |
| 11 guitar strum calls | Source short/medium/long strokes and rest; extra long variants reuse source long strokes |

There are no authored directional turn clips in this source set. The recipe
adapts source walk starts with a continuous heading change. The generic
`build_gh2_locomotion_graph.py` matches existing body poses to select transition
times and adds stationary facing channels for GH2's predictor. It does not edit
body pose keys. The MILO writer accepts `--transitions`, validates every link,
and serializes the native transition graph. In the native integration CharWalk
owns world movement once; the servo still publishes the full body pose.

`fit_gh3_ps2_hand_calls.py` proposes disjoint source finger layers using stock
distal-joint bend signatures. Its score is not proof of guitar contact. Thumb,
wrist and finger proportions remain those of the source character; these are
functional call adaptations, not replicas of Casey's hand poses. Review fretting,
high variants and vibrato in game after changing the source rig or instrument.

The native guitarist scope excludes the six bassist slap/pluck calls. Retail
interaction/exclusion consumers and execution on a retail PS2 GH2 build remain
unverified. Native success must not be advertised as retail compatibility.
Retired donor-rig scripts are not part of this pipeline.

## Required animation-call review for future conversions

Rig compatibility and a playing clip do not establish animation-call compatibility.
Audit every converted character against the target bank inventory:

```powershell
python tools/audit_gh2_character_calls.py --stock-hdr "path/to/GEN/main.hdr" --stock-ark "path/to/GEN/main_0.ark" --package "path/to/DLC/package" --character gh3_midori --stock-character rock1 --recipe tools/gh3_ps2_recipes/midori_actions.json --output "call-inventory.json"
```

This reads stock banks directly from the archive and all outfit bank references
from the candidate manifest. It checks named clips, groups, missing/empty/dangling
group members and duplicate names, and records recipe aliases. Exit 1 indicates
incomplete inventory; exit 0 means name coverage only, never full compatibility.
The report always leaves full compatibility unproven. Stock alternative clip names
are a conservative inventory: required dispatch contracts must be distinguished
from optional variants rather than filling every name with the same idle clip.

For each required target action, establish the corresponding source action,
correct group membership and filtering, tempo, looping and transition behavior,
events, bone masks and layer ownership. Then exercise real GH2 dispatch and review
the resulting motion. Unsupported source-only animations may be dropped; required
GH2 actions cannot silently fall back to idle and count as completed mappings.
This applies to subsequent Neversoft conversions as well as Midori.

The builder additionally checks `gh2_native_guitarist_contract.json` before
conversion: every required direct call and group must exist, groups must have
playable members, and aliases cannot collide. Keep this consumer contract when
adapting a new character; change the source mappings and prove their behavior.
Optional stock variants are still shown as gaps by the conservative inventory.

Native verification uses actual stock `char_objects.dtb` handlers and the normal
driver resolver. `GHOGX_DIAGNOSTIC_CHARACTER_MESSAGES` accepts
`song_seconds:role:handler` entries for process-local dispatch tests. This does
not send OS input or substitute a forced rendered pose. Star Power should also
be tested through the guitar input script, and win/loss through actual song
completion/failure. `GHOGX_DIAGNOSTIC_ENDING_HOLD_SECONDS=12` extends only the
diagnostic result display so a long outro can be inspected after the audio stops.

Run the converter contract tests with `MILO_CONVERT_TOOL` set to the built tool:

```powershell
python -m unittest discover -s tools -p "test_gh3_ps2_*.py"
```

Then run the native character type-script and all-pages clip-binding tests,
capture both outfits playing, exercise transient-action return and walking,
and verify the final content hashes. Remove disposable build and capture trees.

## Menu presentation

Manage Band uses the same `ui_loop` idle path as multiplayer character select;
it never invokes the 1P entrance. A camera fitted once to each posed model
centers the preview at 20% of screen width, inside the left 40% bay. Career
continues to play `ui_enter` followed by `ui_loop` using its authored placement.
Midori 0.2.1 replaces the incompatible out-to-A / out-idle pairing with source
frontend intro2 and its held terminal body pose. `cycle_hold_overlays` keeps the
longest face/accessory cycle intact and fits whole cycles of shorter overlays
into the same interval, retaining source poses without resetting mid-key.
