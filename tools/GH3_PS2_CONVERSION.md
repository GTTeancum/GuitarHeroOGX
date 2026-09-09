# GH3 PS2 source character conversion

This pipeline preserves the PS2 source skeleton, skinning and animation samples,
renames the target bone channels, derives GH2 arm frames, and retains source
instrument proxies. Both Midori outfits were reviewed in native gameplay and
approved for local DLC installation on 2026-09-08.

Requirements: Python 3.12, numpy, scipy, Pillow; the repository's
milo_convert_tool (build tools/milo_convert with CMake); a GH3 PS2 USA ISO;
and an external https://gitgud.io/fretworks/nxtools.git checkout. The source
proof used NXTools commit 6cea808. No game assets or NXTools are bundled here.
Run from the repository root:

```powershell
python tools/gh3_midori_ir.py --iso "path/to/Guitar Hero III - Legends of Rock (USA).iso" --nxtools "path/to/nxtools" --output "scratch/ir/midori_source_ir_manifest.json" --asset-dir "scratch/ir"
python tools/build_gh3_ps2_playing_probe.py --source "scratch/ir" --converter "path/to/milo_convert_tool.exe" --recipe tools/gh3_ps2_recipes/midori_1.json --work "scratch/base" --output "output/base/community.gh3.midori"
python tools/build_gh3_ps2_playing_probe.py --source "scratch/ir" --converter "path/to/milo_convert_tool.exe" --recipe tools/gh3_ps2_recipes/midori_2.json --work "scratch/alt" --output "output/alt/community.gh3.midori"
```

Use separate empty work/output directories. Each command creates a standalone
outfit package with the same package ID; do not install the standalone packages
together. To combine outfits, retain the four shared banks, include both model
files, merge the manifest outfits/files, and rebuild the content index hashes.
The reviewed combined package verified all four banks identical between outfits.
The builder produces a candidate and does not install it automatically.

The recipes retain explicit source-to-instrument frame mappings and clip aliases.
Hand attacks use immediate playback and no body-style crossfade. Generic rigid
frame rebasing compensates descendants and checks world-pose invariance.
The milo builder's --preserve-guitar-proxies prevents replacement of source IK
helpers; generated root bone names also follow the normal GH2 channel suffix rule.

Current scope: one playing body loop with face/accessory overlays and source hand
clips; the body bank is also a UI placeholder. Full action coverage, loop seams,
complete hand-call mapping and retail GH2 compatibility remain unverified. Local
native gameplay approval does not establish retail compatibility. Retired donor
rig scripts in this repository are not part of this pipeline.

## Required animation-call review for future conversions

Rig compatibility and a playing clip do not establish animation-call compatibility.
Audit every converted character against the target bank inventory:

```powershell
python tools/audit_gh2_character_calls.py --stock-hdr "path/to/GEN/main.hdr" --stock-ark "path/to/GEN/main_0.ark" --package "path/to/DLC/package" --character gh3_midori --stock-character rock1 --recipe tools/gh3_ps2_recipes/midori_1.json --output "call-inventory.json"
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

The 2026-09-08 installed Midori audit FAILS completeness: main 1/113, UI 1/2,
fret 9/25 and strum 4/17 stock clip names covered; 29 main groups missing,
including star_power, solo, intro, win and lose. Installation was authorized for
the reviewed visual candidate; it did not establish complete animation mapping.
