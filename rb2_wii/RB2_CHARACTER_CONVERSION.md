# Rock Band 2 Wii preset-character conversion

## Scope

The first release-pipeline characters are retail Rock Band 2 Wii prefabs
`guitar32` (**The Duke of Gravity**) and `vocals20` (**Penelope McQueen**).
Both are packaged as additive loose DLC with unique character IDs; neither
replaces a native GH2 character or outfit. No extracted commercial source
payload is committed to this repository.

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
deterministic cosine-hemisphere rays. Duke uses 8 samples, a 5-unit maximum
distance, 0.06 bias, and a restrained 15-percent maximum darkening. Transparent
materials are excluded so alpha elements are not damaged. The audit records
the source/atlas counts, placement, AO parameters, and resulting texel data.

## Native GH2 package

The mesh-bundle stage retains 95 source render meshes, 6,023 vertices, 7,135
faces, source skin weights, bind transforms, and render flags. The generated
donor is merged into the source-audited `classic` compatible target with:

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
   machine-readable source/deform/material/atlas/AO audit.
3. Run `milo_convert_tool build-character-from-meshbundle`, supplying the
   chosen GH2 main, strum, and fret banks.
4. Run `milo_convert_tool merge-character-render-payload` with
   `--rebind-template-rig`. Do not preserve donor hand-slot offsets when the
   source and target bind poses differ: every connected skin slot must be
   rebased into the same GH2 bind space or sleeves and hands separate.
   Facial presets additionally use `--retarget-rb2-face-rig`, which maps the
   RB2 facial schema onto the selected GH2 donor's authored facial controls.
   Mesh-based eye pivots use their hierarchy-derived bind world rather than
   the stale serialized world cache carried by some retail characters.
5. Run `package_rb2_character.py` with an additive manifest, merged model, and
   portrait source. Pass each declared model-relative facial or animation
   dependency with `--extra-file SOURCE=MANIFEST_PATH`. The packager validates
   the exact declared file set, writes the loose-DLC tree, and emits a sorted,
   hashed content index. It then reopens the finished package and verifies every
   payload size and SHA-256 against that index. Before deployment or testing an
   existing package, run the same command with `--manifest`, `--out`, and
   `--validate-only`; this rejects stale models hidden behind newer index data.
6. Run the native game with the package mounted. Accept only with logged model,
   animation-bank, hand-map, prop-attachment, asset-resolution, and performance
   evidence plus active-playing visual proof.

## Duke acceptance result

The native game discovers `DLC/rb2.duke_of_gravity`, resolves selection
`rb2_duke_of_gravity_default`, loads all 95 intended meshes and 10/10 requested
textures, loads the GH2 main/strum/fret banks and 31 hand-map clips, and attaches
the guitar to `bone_pos_guitar.mesh`. Three active-playing frames show both
hands staying with the guitar across changing poses. The proof run reached
60.018 steady-state FPS / 16.662 ms at 960x720.

Proof assets and the release audit are under `proofs/rb2-duke-pipeline/` in the
local worktree. The generated retail-derived model remains ignored; the
manifest and pipeline code are versioned.

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
