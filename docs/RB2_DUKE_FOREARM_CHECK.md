# Duke forearm check — 2026-09-09

## Accepted and installed: DLC 0.2.1

The user accepted the corrected forearms and left hand on 2026-09-09 with
“Passed.” Implementation and proof are committed in `6332526c`. The canonical
process is [RB2 character conversion](../rb2_wii/RB2_CHARACTER_CONVERSION.md).

This result supersedes the failed rerun below. Both forearms now remain connected
in sampled native gameplay and Manage Band captures. The final headless playing
run recorded 27 hits, zero misses, and zero overstrums. Proof:
`proofs/rb2-duke-forearms-20260909/fixed-playing.mp4`, `fixed-playing.png`,
`fixed-menu.png`, and `fixed-verification.json`. Visual review sampled individual
frames; it does not claim exhaustive frame-by-frame inspection or retail PS2 execution.

There were two defects. First, the cached Duke donor had morphed vertices but
stale skin offsets. It predates the source bind correction in `4b4f3791e`.
The new raw-source validator rejects it with a maximum offset error of 1.2992;
rebuilding from the same raw assets passes at 0.0000061. The source vertices,
weights, and atlas UVs are identical; the bind frames were wrong. This displaced
the fingers relative to their joints and made the first arm repair's left hand
look stretched. That candidate was rejected and removed from both installs.

Second, the old release mixed source and target arm binding strategies.
The reusable converter option `merge-character-render-payload
--retarget-template-bind-pose` converts **vertices and normals as well as skin
offsets** into the target's bind pose. Merely rebasing offsets leaves the source
finger coordinates in the wrong frame; preserving those offsets alongside
rebased sleeve slots disconnects the arm. This mode adapts the geometry to the
target skeleton's bind proportions and retains source weights, UVs, topology,
materials, and the target's existing controllers and animation banks.

For a source bone that collapses onto an animated ancestor, the conversion uses
that ancestor's source bind frame. Applying the discarded facial helper's
inverse bind to the head target incorrectly moves each face part independently.
There are no Duke-specific bone offsets, position nudges, or authored animation
changes. Nonuniform-scale normals use an inverse transpose; mesh and character
bounding spheres are regenerated. Mixing this mode with offset preservation is
rejected.

Rebuild the donor from `proofs/rb2-duke-pipeline/components` using
`convert_rb2_preset_character.py`, the Duke recipe, the `target-classic`
transform directory, and `source-inspection/male_guitar_deform/CharClipSamples__deform`.
Pass the resulting mesh bundle to `build-character-from-meshbundle --name classic`
with the native `classic_main`, `classic_strum`, and `classic_fret` animation paths.
Do not reuse the historical `rb2_duke_source.milo_ps2` without validation.

Export the rebuilt donor with `export-character-snapshot` and run
`validate_rb2_character_bind.py --snapshot <snapshot> --component-root <components>
--recipe <recipe> --deform-clip <deform> --audit <report>`. This compares all source
vertices, weights, and 345 skin slots with a fresh raw-source bake, before any
target retargeting. It fails the old donor and passes the rebuilt one.

Then merge the validated donor:

```powershell
milo_convert_tool merge-character-render-payload tools/milo_convert/gh1-character-models/classic.milo_ps2 --donor <scratch>/rebuilt-source.milo_ps2 --retarget-template-bind-pose --out <scratch>/duke-rig.milo_ps2
```

For the geometry-only rebuild, disable AO in a scratch copy of the recipe and
restore the existing baked atlases afterward. Extract the converted output
and the existing 0.2.0 package with `milo_tool extract`. Copy the existing three
`Tex__rb2_duke_of_gravity_atlas_*.tex` bodies into a small texture donor folder;
run `rb2_wii/tools/refresh_rb2_baked_textures.py` with the new rig extraction as
`--base-extracted` and that folder as `--donor-extracted`. This restores the
accepted baked RGB data without changing texture headers, alpha, or other
payload bytes. A fresh source bake can supply those atlases on future builds.

Run `milo_tool verify`, update the package index and version, then run
`package_rb2_character.py --manifest <manifest> --out <package> --validate-only`
before and after deployment. Both workspace and live DLC installs were verified.

Validation: 95 source meshes, 6,023 vertices, 345 rebased slots, 130 mapped bone
references, and all 17 template controllers preserved. Snapshot comparison
checks that all weights, UVs, and triangle indices are unchanged, and all 1,176
cross-mesh pairs sharing a source position remain coincident after conversion
(maximum gap zero). All 32 installed material/texture bodies match the former
package. `skin_bind_retarget_test` covers slot-order invariance at a seam,
rotation/translation, nonuniform-scale normal orthogonality, and rejection of
empty weights and singular blends. The final converter reproduces identical
model bytes. No shared runtime change was needed for this repair.

## Earlier failed rerun (superseded)

A fresh headless native gameplay run reproduces the forearm issue in the
currently installed Duke of Gravity model. The run used Shout at the Devil,
expert chart input from 30 seconds, and recorded 30 hits with zero misses.
The deployed model was not replaced by either experimental candidate.

Evidence: `docs/proofs/rb2-duke-forearms-20260909/deployed-duke.mp4`,
`deployed.png`, and `verification.json`.

The original conversion mixed two strategies: 235 target-rebased skin slots
and 110 donor-preserved hand-connected slots across 30 meshes. An audit of the
installed payload confirms the same split. This is a useful lead, not proof
that blindly normalizing every bind matrix is a valid repair.

Two additional candidates were rejected:

1. Rebasing the 110 hand-connected offsets to the current target bind graph
   achieved a small mathematical bind residual but visibly stretched fingers.
   A small residual does not establish correct source hand coordinates.
2. Merging with all 345 source bind offsets avoided that particular finger
   distortion but broke head placement. It is not a valid whole-character fix.

The exploratory offset-rebinding command was removed rather than added to the
release converter. The deployed geometry, textures, skeleton, animations, and
DLC index are unchanged. A future repair must reconcile the source arm/hand
vertex coordinates with the target rig and validate active motion; neither
per-character position nudges nor the rejected bind-only substitutions are an
accepted solution. This record is a completed rerun, **not a resolved forearm
acceptance result**. Earlier Duke acceptance notes do not supersede this finding.
