# Rock Band 2 Wii preset-character conversion

## Scope

The first release-pipeline characters are retail Rock Band 2 Wii prefabs
`guitar32` (**The Duke of Gravity**) and `vocals20` (**Penelope McQueen**).
Both are packaged as additive loose DLC with unique character IDs; neither
replaces a native GH2 character or outfit. No extracted commercial source
payload is committed to this repository.

**Current accepted reference:** Duke DLC 0.2.1, accepted by the user on
2026-09-09 after the forearm and left-hand correction in `6332526c`.
The workflow below supersedes the earlier mixed-bind recipe. Duke's acceptance
does not imply acceptance of Penelope's remaining visual issues or execution
on a retail PS2. Character-specific results remain separate below.

## Source facts

`presets/duke_of_gravity.json` is a declarative conversion recipe derived from
the retail prefab and resource objects, not from visual guesses. Duke is a male
guitarist on `male_guitar`, with height `0.75` and weight `0.5`. The recipe
records his ten selected character-creator components, fourteen material
recipes, palette indices, transparent goggle-lens treatment, and target guitar
attachment transform.

The retail `config/prefabs.dta` entry also identifies his fallback instrument:
`stratocaster02_paint` with paint indices `0` and `4`. The DLC manifest carries
that fact as `preferred_guitar`, `preferred_guitar_finish`, and two paint
indices. It applies only when the player has not selected a guitar; explicit
player preferences remain authoritative.

Penelope's retail prefab declares the female `kGenreSpazz` skeleton family,
height `0.25`, weight `0.5`, and the authored `hippybangs_maohat`,
`puffedsleeves_leather`, `puffyskirt_barelegs`, and `thighhighheel_pvc`
components. Her head, eye, lip, makeup, palette, and preferred-guitar choices
are likewise recorded in `presets/penelope_mcqueen.json`; no visual substitute
or per-character transform correction is used.

## Source-authored body deformation

RB2 does not store each create-a-character body as a manually scaled mesh.
`char/male/anim/gen/male_guitar_deform.milo_wii` contains a revision-14
`CharClipSamples` object with an embedded revision-16/14 `CharBonesSamples`
pair. `tools/rb2_deform.py` decodes that stream.

The relevant Wii facts are:

- contiguous sample stride is 500 bytes; the runtime allocation is aligned to
  512 bytes, but the serialized samples are not padded to that boundary;
- compression-2 vector channels use a fixed scale of `1300.0`;
- sample order, corroborated by source editor commands and decoded values, is
  neutral, tall/fat, short/fat, tall/skinny, short/skinny;
- the continuous height/weight surface is four triangles meeting at neutral,
  so evaluation uses barycentric weights rather than bilinear mesh scaling.

For Duke, `(height=0.75, weight=0.5)` produces sample weights
`[0.5, 0.25, 0, 0.25, 0]`. Sample-zero reconstruction matches the authored
neutral transforms within `0.000003425`. The converter evaluates all position,
scale, and quaternion channels, builds neutral-to-shaped bone matrices, and
skin-bakes them into every vertex and normal. It affects all 6,023 vertices
through 83/83 mapped transform channels; the measured maximum displacement is
`1.052219` and RMS displacement is `0.727009`. This preserves the prefab's
source proportions without a Duke-specific scale or limb adjustment.

`tools/test_rb2_deform.py` covers sample parsing, triangle weights, and
transform application independently of the character recipe.

## Materials, atlases, and AO

The converter builds source-colored RGBA images from the authored diffuse,
mask, palette, and palette-index data. RB2 ``*_comp`` textures are runtime
compositor outputs and are never accepted as source masks. For one-color
materials, the retail compositor interpolates from the selected palette color
to the material's unchanged white second color using diffuse alpha; this keeps
fixed-color texels such as eye sclera white while tinting the iris. Material
render flags remain source derived; in particular the goggle lenses retain
blend mode 3 and alpha.

The general atlas stage packs Duke's fourteen material maps into three
512-by-512 pages, rewrites UVs, duplicates edge gutters where space permits,
and retains full-resolution 512 maps rather than resampling them. Atlas names
come from the recipe's `asset_prefix`; they are not character-name branches in
the converter.

The AO stage is real nondirectional ambient occlusion, not painted venue
lighting. `tools/rb2_ambient_occlusion.py` constructs a triangle BVH and casts
deterministic cosine-hemisphere rays. Both selected presets now use 64 samples,
a 5-unit maximum distance, 0.06 bias, and 38-percent maximum darkening.
Transparent materials are excluded as receivers and occluders, preventing
solid shadows from alpha cards. Shared UVs use the maximum occlusion rather
than letting an unoccluded triangle erase a contact shadow. The audit records
the source/atlas counts, placement, AO parameters, and resulting texel data.

## Native GH2 package

The September 8 shared-parser fix removes the major menu-load bottleneck.
Modern MILO directories with one terminator per object now validate their
forced boundaries iteratively; the general recovery path skips partitions
that cannot leave enough terminators for the declared objects. Ambiguity and
embedded-marker behavior remain intact. Native object listings for both RB2
packages are byte-identical. Measured submenu-to-preview time fell from
5.10 to 0.38 seconds for Duke and 16.90 to 0.36 seconds for Penelope, versus
0.37/0.30 seconds for the stock Clive/Casey previews in the patched run.
These are single fresh-process measurements, not disk-cold averages. See
`RB2_PARSER_FIX_REPORT.json`. No character geometry or rig data was removed.
Those timings are historical. The later recurrence came from a stale compiled
parser library; `56695d8f` commits the fix and bounded-work regression test.
See `../docs/RB2_LOADING_REGRESSION.md`. Rebuild the parser library as well as
the executable, and do not restore stale objects during build cleanup.

The September 8 texture refresh preserves the deployed rigs, geometry, and
facial dependencies byte for byte. `refresh_rb2_baked_textures.py` replaces
only matching, same-size texture bodies from a newly baked donor. Native
extraction verifies every other object is unchanged; native container
verification and package index validation follow before deployment. The
converter's optional `--texture-preview-dir` exports material PNGs for review.
Headers and alpha must also match exactly: only RGB values may change.
Penelope's blouse and skirt opt into `palette_detail_floor: 32`, preserving
diffuse folds that an exact-zero black tint otherwise multiplies away. This is
a deliberate diffuse-only material adjustment; source palette indices remain
recorded in the recipe and all other materials retain their authored tint.

The extracted Wii resources for these presets contain no normal/bump-named
texture assets. The bake retains the authored diffuse detail and adds
geometry-derived contact shading; it does not invent a normal map.

Duke remains at 7,135 triangles and Penelope at 6,929. The retained stock donor
geometry, excluding lower LODs, shadow meshes, and named facial pose targets,
is 6,248 triangles for Clive and 6,387 for Casey. These are geometry inventory
comparisons, not measured draw calls. The RB2 models are approximately 14% and
8% higher, respectively. This pass retains their existing silhouettes, joint
seams, and facial detail without decimation. It does not close the pre-existing
Penelope facial/necklace acceptance issues described below.

The mesh-bundle stage retains 95 source render meshes, 6,023 vertices, 7,135
faces, source skin weights, bind transforms, and render flags. The generated
donor was originally merged into the source-audited `classic` compatible target
with the following split. **Duke 0.2.1 supersedes this split:** all 345 slots and
their vertices now use the target bind pose, fixing the detached forearms.
The old split is retained here to identify obsolete packages:

- one GH2-compatible target skeleton and controller graph;
- 235 body slots rebased to the target bind graph;
- 110 hand-connected slots preserving donor bind offsets across 30 meshes;
- 130 mapped bone references with maximum bind residual
  `0.00000762939`;
- two upper-arm twists, two forearm twists, two IK hands, one IK MIDI, three
  drivers, and two weight setters.

The package references the native Clive/`classic` main, strum, fret, and UI
animation banks. It reuses their animation assets; it does not replace Clive's
character ID, outfit, or model. The normal GH2 guitar prop attaches to the
authored `bone_pos_guitar.mesh` transform.

## Reproducible pipeline

1. Extract only the recipe's resources and the relevant deform clip from the
   user's RB2 Wii content with `milo_tool`.
2. Run `convert_rb2_preset_character.py` with the component root, recipe,
   target-transform root, and `--deform-clip`. It emits a mesh bundle and a
   machine-readable source/deform/material/atlas/AO audit. Bake the authored
   height/weight deformation into both vertices and skin bind frames. A cached
   donor with the right vertices, textures, or file timestamp is not sufficient.
3. Run `milo_convert_tool build-character-from-meshbundle`, supplying the
   chosen GH2 main, strum, and fret banks. Export this **source donor**, before
   target merging, with `export-character-snapshot`. Run
   `validate_rb2_character_bind.py` against the original components, recipe,
   and deform clip. Require exit zero; rebuild a failing donor from source.
   This checks mesh inventory, vertex positions, weights, and every bind slot.
4. For conversion onto a stock target rig, use
   `merge-character-render-payload --retarget-template-bind-pose`. This is the
   accepted Duke path: it adapts vertices, normals, and skin offsets together,
   resolves discarded helper bones through their surviving source ancestors,
   and updates bounds while retaining the target controller graph. Do not mix
   this mode with `--preserve-donor-bind-offsets` or
   `--preserve-donor-hand-mesh-bind-offsets`; the converter rejects that mix.
   Rebasing offsets alone can stretch fingers, and mixing preserved hand slots
   with target-rebased sleeves can separate the forearms. Do not compensate
   with character-specific position nudges or authored pose patches.
5. Treat facial and source-proportion retargeting as separately validated rig
   choices. `--retarget-rb2-face-rig` maps supported RB2 facial channels onto
   the donor's controls; it does not establish visual acceptance by itself.
   Preserve required FaceFX/viseme dependencies and use hierarchy-derived bind
   worlds for mesh-based eye pivots. Do not automatically rebuild Penelope with
   Duke's rig strategy merely because Duke passed.
6. Retain accepted baked materials during geometry repairs. If rebaking AO is
   unnecessary, disable it only in a scratch recipe, retain the atlas layout,
   and restore accepted atlas bodies with `refresh_rb2_baked_textures.py`.
   Require matching dimensions, headers, and alpha; verify that only intended
   RGB data changes. Keep the authored palette and source detail. Run native
   `milo_tool verify` on the finished model.
7. Run `package_rb2_character.py` with an additive manifest, merged model, and
   portrait source. Pass each declared model-relative facial or animation
   dependency with `--extra-file SOURCE=MANIFEST_PATH`. The packager validates
   the exact declared file set, writes the loose-DLC tree, and emits a sorted,
   hashed content index. It then reopens the finished package and verifies every
   payload size and SHA-256 against that index. Before deployment or testing an
   existing package, run the same command with `--manifest`, `--out`, and
   `--validate-only`; this rejects stale models hidden behind newer index data.
8. Test the exact indexed package headlessly through the native game. Capture
   Manage Band and actual guitar gameplay, including close views of both hands,
   wrists, elbows, and sleeves across changing poses. Check fretting and
   strumming, facial placement, props, and the complete silhouette. Compare a
   stock donor when a pose or deformation is uncertain. Use native captures and
   process-local chart input; never drive the host desktop. Retain asset and
   animation-resolution logs plus compact numerical evidence. Seam and bind
   metrics supplement visual review; they cannot approve a distorted hand.
9. Present the visual proof and record the user's acceptance against the exact
   package version/hash. Validate the installed index after copying to each
   destination. Commit the reusable tools, tests, recipe/manifest/index, process
   record, and smallest useful proof. Keep extracted commercial assets and
   generated models out of Git. Remove temporary builds, extraction trees,
   logs, and bulk captures; preserve only deliverables and compact evidence.

### Source-bind validation command

Use paths for the same source recipe and deformation that built the donor:

```powershell
milo_convert_tool export-character-snapshot <source-donor.milo_ps2> --out <scratch>/source.snapshot
python rb2_wii/tools/validate_rb2_character_bind.py --snapshot <scratch>/source.snapshot --component-root <components> --recipe <recipe.json> --deform-clip <CharClipSamples__deform> --audit <scratch>/source-bind.json
```

The validator's tolerance is 0.0001 for vertex-position and bind-offset error.
Duke's stale donor failed with offset error 1.2992 even though its vertex
positions and weights were correct. The fresh donor passed at 0.0000061.
The source bind correction already existed in `4b4f3791e`; cached intermediates
had bypassed it. Run this gate on reused donors as well as new conversions.

## Duke acceptance result

Duke 0.2.1 was accepted by the user on 2026-09-09: **“Passed.”** The installed
model SHA-256 is `71ef19b9ea8a106cd00a969c005e6837c5c52061863108d3711ebf18f45f8333`.
Both workspace and live DLC indexes were verified. The conversion retains
95 meshes, 6,023 vertices, 7,135 triangles, 345 consistently rebased slots,
17 template controllers, and all 32 prior material/texture bodies. All 1,176
matching cross-mesh source seam pairs remain coincident after retargeting.

The final native run recorded 27 hits, zero misses, and zero overstrums.
Individual full-body and close-up frames and a 1920x1440 Manage Band capture
were inspected. This is sampled visual review, not every-frame inspection.
The accepted proof and reproducible repair record are:

- [Gameplay clip](../docs/proofs/rb2-duke-forearms-20260909/fixed-playing.mp4)
- [Menu screenshot](../docs/proofs/rb2-duke-forearms-20260909/fixed-menu.png)
- [Verification](../docs/proofs/rb2-duke-forearms-20260909/fixed-verification.json)
- [Repair and reproduction details](../docs/RB2_DUKE_FOREARM_CHECK.md)

Earlier mixed-bind and stretched-hand captures are superseded. They must not
be used as the release acceptance reference.

## Penelope work-in-progress result

The first native retarget proof was invalid: its loose-DLC index described the
current model while the package still contained an older payload. That stale
model split both arms across shoulder, upper-arm, elbow/cuff, forearm, wrist,
and hand pieces, placed both eye meshes above the head, and rendered the
necklace's transparent card as an opaque panel.

The corrected package is hash-validated before launch. A 65-frame active-pose
seam trace checked 1,528 matching source-vertex pairs per frame with no pair
over 0.05 units and a maximum separation of 0.000031 units. Four later active
playing captures at frames 60, 120, 180, and 240 were inspected individually:
the eye meshes remain in the head and both complete arm chains remain connected
through their cuffs and hands. The proof run held 60.018 steady-state FPS.
Penelope is still work in progress: the mouth deformation is visibly malformed,
and the necklace alpha retains substantial edge/background artifacting. Those
fail visual acceptance and must be corrected before release.

## Remaining roster-scale boundary

Duke establishes the reusable body-shape, material, atlas, AO, rig, hand, prop,
and additive-package path. A selected release roster still needs per-prefab
recipes, source-appropriate GH2 animation-donor selection, final GH2-style
portrait art, facial-animation parity where the source preset uses expressive
face data, and the same native acceptance matrix for every selected character.
Those are tracked as open work; they must not become per-character transform or
attachment exceptions.

## Penelope facial-retarget result

Penelope uses Casey Lynch's `rock1` GH2 main, UI, strum, and fret animation
banks. The reusable RB2 facial-schema bridge maps her source eye meshes to
Casey's two live `CharLookAt` pivots and maps her jaw, upper/lower lip,
lip-corner, brow, cheek, and upper-lid weights to Casey's native facial bones.
The package carries Casey's retail viseme bank at the model-relative path
requested by the preserved `FaceFxLipSyncServo`; it does not duplicate or
replace Casey's model.

`validate_rb2_face_retarget.py` binds the static conversion audit to a native
runtime log. The accepted Penelope output has 37 weighted vertices on each eye,
626 mouth-related weighted-vertex references across nine facial targets, the
unchanged 25-node/11-pose Casey FaceFX graph, all 187 runtime viseme channels,
and 258 sampled matrices for each live eye. Both eye matrices changed during
the native gameplay run. Casey's authored viseme set independently contains
nonzero jaw rotation and lower-lip translation, so the mouth result is proven
from authored animation data rather than visual inference.

Penelope's retail hair and heavy eye-shadow makeup intentionally obscure most
of the eyes in ordinary gameplay. Diagnostic captures may suppress the hair
material to inspect controller behavior, but the distributed package retains
the authored appearance.
