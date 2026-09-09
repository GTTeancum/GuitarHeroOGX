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
