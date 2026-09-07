# Guitar Hero 1 venue compatibility

## Scope and delivered output

The delivered path is deterministic offline conversion of the seven retail
Guitar Hero 1 venues into persistent GH2 revision-24 assets. GH2 remains the
authoritative gameplay, highway, HUD, scoring, camera-manager, and UI corpus;
the selected GH1 venue supplies only its authored world, cameras, lighting,
crowd, particles, props, scripts, and event data. Runtime GH1 decoding remains
an evidence route, not a deployment dependency.

Authored venue animation is required throughout the performance, not only
during the introduction. Retail GH2 venues have continuous movement and life,
which is the behavior baseline: GH1 intro handlers, ongoing object animation,
View propagation, environment/light animation, particles, visibility changes,
and excitement/section transitions must execute in sync with each venue's
native cameras. This includes the basement stair descent and continued
post-intro motion. A correct static frame is not completion.

Regular GH1 `VenueCam` paths remain venue-space paths, while their framing
subject is the singer. Close-shot validation therefore requires a resolved
singer transform even when character meshes are hidden from a venue proof.
The GH1 ARK has no GH2-style `<venue>_chars.milo_ps2`; use a source-backed GH1
performer placement/A-pose target or the eventual GH2-character hybrid graph.
Do not compensate for a missing subject by rescaling the authored camera path.

GH1 song quickplay records may omit both the selected guitarist and band.
Source placement and role facts are read from `config/gen/characters.dtb`,
`charsys/gen/charsys.dtb`, and `charsys/gen/band_chars.dtb`; they are compiled
into the persistent target packages. No `characterLimits.mesh` grid, venue
identity, or screenshot-derived placement correction participates in final
conversion.

Converted venues live at `world/gh1_<venue>/gen`. This prevents GH1 names such
as `arena` from replacing the GH2 venue of the same name and permits arbitrary
GH1/GH2 song, venue, and character combinations. The same manifest bytes can
run loose or be applied to the base ARK. Runtime conversion and generated
asset caches are not used.

## Evidence policy

- ihatecompvir's Mackiloha/Milo sources are a source-backed structural reference.
- GH1 and GH2 retail assets are the serialized-data authority.
- Decompiled/recompiled game routines are the behavioral authority when field
  meaning cannot be established from serialization alone.
- A format claim is recorded as verified only after the reader consumes the
  complete object body and the result agrees with multiple object instances or
  a corresponding game routine.
- Visual proof uses individual on-disk screenshots, not contact sheets or logs.

## Integrated playable and character proof

The native-read path runs GH1 bonus song `hey` through GH2 gameplay while
retaining the GH1 band and venue. The chart is 167.3 seconds with 272 Easy
notes and 481-note Medium/Hard/Expert lanes. Its four-channel 44.1-kHz VGS
streams successfully. A deterministic 30-second seek plus autoplay produces
one GH2-authoritative hit, score 50, streak 1, and no miss.

Highway and gameplay HUD are disabled without suppressing performers. The
native GH1 `metal` guitarist, singer, bassist, and drummer RndDirs decode every
mesh and load every requested texture. The same assembly renders in all seven
GH1 venue routes; evidence is in
`.codex/current-evidence/gh1-all-venues-character-playable-audit/`. The
integrated chart/audio/scoring proof is in
`.codex/current-evidence/gh1-playable-integrated-proof/small_club/`.

The GH1 campaign song-opening overlay is also native-read. The runtime follows
`ghui/gen/game.dtb`'s 1000-millisecond task, loads LabelEx/BandLabel placement
from `ghui/mtv_overlay.gh`, resolves text resources and colors from
`ghui/gen/config.dtb`, and follows the named Font -> Mat -> Tex graph in
`ghui/gen/resources.rnd_ps2`. Direct gameplay and the normal menu-to-gameplay
path use the same component. Format and proof details are recorded in
`.codex/analysis/gh1-mtv-song-overlay-format.md`. A layered normal-menu proof
also verifies the previously completed generic GH1 VenueCam adapter running
the authored opening path at the same time, so the complete opening
presentation is now closed.

For normal GH2 retail-menu play, the UI and gameplay archives may be mounted
independently with `--ark-dir` plus `--content-hdr`/`--content-ark`. The GH2
front end remains authoritative; only its song catalog is replaced, and the
selected song's chart/audio, native GH1 venue/band, and opening overlay all
resolve from the content mount. This avoids repacking, runtime conversion,
generated caches, and song-specific routing.

GH1 singer, bassist, and drummer RndDirs do not contain later GH2 controller
graph classes, but their CharSys directories do contain native raw ACP body
clips. Guitarists use character-owned stems; singer, bassist, and drummer use
role-owned stems (`singer_*`, `bassist_*`, and `drummer_*`). These retail clips
now resolve in gameplay. No controller, IK, viseme graph, or absent intro clip
is synthesized where retail data lacks it. The exact layout and validation are
recorded in `.codex/analysis/gh1-role-acp-resolution.md`.

## Offline GH2-native venue closure

The converter emits a 62-row venue bundle:

- 33 revision-24 MILO directories;
- 21 campath, sequence, event, sound, and other support files;
- seven compiled native venue scripts; and
- one shared crowd-animation package.

All output paths are namespaced below `world/gh1_<venue>/gen`. The seven venue
scripts total 886,740 bytes and have zero blocked operations. The packed venue
object sweep accounts for 14,581 schema-typed references and rejects a package
if an internal material, texture, geometry owner, parent, animation target,
Group member, or camera dependency is dangling.

Every main converted venue now owns a real GH2 WorldDir11 root rather than a
generic RndDir root. All 10 packed `{arena load_section ...}` directives
resolve to converted lighting/crowd RndDirs and are linked through native
ObjectDir16 subdirectories; Fest's top-level `reactorState` initializer becomes
a WorldDir type property. No section name or venue name selects a conversion
exception.

Each of the 201 GH1 VenueCam records is emitted as a native GH2 revision-20
CamShot, for a total of 191,320 native keyframes and zero blocked records.
Converted `campaths.milo_ps2` lives beside the target world; source
`camera.dtb` is not shipped as a runtime dependency. Native CamShots are
authoritative, and the compatibility reader is considered only when the
native camera pool is empty. The final Small Club proof starts on native
`Intro01` and transitions to regular native `flr_near_lft01` through the
ordinary camera manager.

**Superseded blanket-reference conclusion (2026-09-03 audit):** the initial
`arena` atom is the message receiver, not a transform-parent declaration.
`camera.dtb` does not contain the complete reference policy. GH1's shared
`arena/gen/cam_paths.dtb` specifies parent and target per path; the executable
queries it at `0x16f078` / `0x16f160` and applies it at
`0x16fbc8..0x16fcc4`. Missing overrides use the player-head helper; explicit
overrides include the venue root, stage spots, singer neck, and conditional
guitar-neck references. The converter now reads that shared policy. Conditional
`exists/else` references remain runtime decisions, not baked roster assumptions.
Already-staged assets still require migration and proof before deployment.

`SLUS-21224:0x0016E080` is only the default target-position resolver, not
`VenueCam::Update`. It reads the target index at `VenueCam + 0x74`
and resolves that index through the ArenaSinger vector at `Arena + 0x3C`.
A read-only retail Basement savestate has target index zero and its selected
transform is the player guitarist, approximately `(26.8,111.5,66.7)`, not the
vocalist. A fresh Basement trace proves both native refs resolve and both frame
target caches populate. The load-log
`source_object=none` annotation only denotes the absence of a later diagnostic
source-object field in revision-20 subpart refs; it is not a resolution
failure. Exact counts and the focused trace are recorded in
`proofs/gh1-native-conversion-parity/venue-camera-reference-contract/`.

The `singer_in/out` labels are historical field names, not performer-role
selectors. Their two floats are the desired centered screen coordinate for the
selected ArenaSinger. GH1 maps `(x,y)` to viewport
`((x+1)/2,(1-y)/2)`. GH2 `CamShot::SetPos` converts a viewport coordinate back
to centered `mScreenOffset` with `((u-0.5)*2,(v-0.5)*-2)`, so the two mappings
are exact inverses. Conversion therefore retains `(x,y)` directly; negating or
halving either axis is incorrect.

Revision-12 Group members are resolved by target directory type after the
complete directory is loaded. Animatable members form a recursive event graph;
drawable members retain draw order. A source-present empty Group is a resolved
no-op. Channel-empty MatAnim objects are likewise resolved no-ops. Direct
EnvAnim and LightAnim objects share the same dispatch route. The venue
`finish_loading` section is a barrier: intro, music, and excitement messages
latched during initial assembly replay after the lighting directory is ready.

The target renderer submits a Mesh only when it has a material, matching the
platform draw boundary. Material-less transform/editor helpers remain in the
hierarchy but do not become white rectangles. Mat21 texture stages lower to
deterministic Mat27 `nextPass` chains, and Tex8 bitmap payloads remain
byte-identical PS2 data inside Tex10 wrappers. No mesh, texture, material, or
venue name controls those decisions.

The final runtime matrix covers Arena, Basement, Big Club, Festival, Small
Club, Small Club Multi, and Theatre. Each run loads native world, native
script, and native lighting paths and exits successfully, with zero raw GH1
script loads, unsupported operations, or unresolved animation targets. The
matrix, intro and regular-camera screenshots, and mixed native video are under
`proofs/gh1-native-conversion-final/`.

A stricter seven-venue matrix now uses `--require-native-assets`, which
disables raw `venues/...`, raw `charsys/...`, legacy VenueCam, and raw
root/sibling section fallback. Every converted root reaches gameplay with
native world/script/lighting paths and four GH2-layout performers. Small Club
resolves drawable geometry from its native WorldDir root and `lighting.milo`
through the authored ObjectDir subdirectory; the lighting section is selected
from decoded object types rather than its filename. Every row contains zero
gate violations or legacy markers, and Small Club contains zero unsupported
revision-4 Morphs. Evidence:
`proofs/gh1-native-conversion-parity/strict-native-venue-matrix/`.

## Verified GH1 venue revisions

Standalone GH1 `Flare` objects use revision 3 with an embedded revision-8
Transform and revision-1 Drawable. Their native material, two screen-size
values, distance range, and step count are retained. Venue
`{flare set_steps n}` messages route to the resolved RndFlare object, including
foreach DataVariable targets; they are not treated as animation frames.
The complete byte order and proof inventory are recorded in
`.codex/analysis/gh1-rndflare-format-and-set-steps.md`.

GH1 venue DataArray control now also retains lazy `if_else`, object `exists`,
boolean/comparison camera expressions, and
`with_namespace {performer geom_space}` lookup. Namespace-scoped character
View/Group visibility is resolved from each decoded character graph rather
than redirected to venue meshes. The exact source contracts and focused
Basement/Small Club regular-camera proofs are recorded in
`.codex/analysis/gh1-dataarray-if-else-with-namespace.md`.

| Object | GH1 revision | Current verified semantics |
|---|---:|---|
| `View` | 7 | Embedded Trans rev8, nested-object list, authored member list, drawable data, trailing environment reference |
| `Trans` | 8 | Local transform plus authored legacy child list; trailing self-reference is not a parent |
| `Mesh` | legacy directory rev10 layout | First declared Mesh body may be omitted; remaining bodies align one entry later |
| `Mat` | 21 | Legacy texture-entry array; selectors 0 and 1 carry sampled 2-D textures, selector 5 is sphere/environment data |
| `Light` | 3 | Embedded Trans rev8, RGBA, range, and light type; later preset booleans are absent |
| `Environ` | 1 | Legacy variable header followed by the fixed payload and validated light-reference vector |
| `TransAnim` | 4 | Embedded Drawable rev1 and native translation keys used by camera paths |

## Verified basement relationships

- All 13 basement Views decode.
- `venue.view` is the authored root and recursively selects 90 venue meshes.
- 34 unreferenced editor/reference meshes are not members of the rendered venue.
- All 90 selected meshes resolve an authored environment through their View.
- The base scene contains 3 decoded lights and 2 decoded environments.
- The lighting archive contains 15 decoded lights and 23 decoded environments.
- `Cam_basement_intro.tnm` contains 88 native translation keys.
- `arena/gen/cam_paths.dtb` parents `Cam_basement_intro` to
  `arena::venue.view`; its translation keys are therefore parent-local.
- The retail INTRO record runs for 10 seconds; its path keys are rescaled to the
  300-frame real-time interval rather than played as a 1,920-frame clip.
- Revision-10 directories contain an external-resource vector but no root
  directory object body. Child object body 0 begins immediately afterward.
  Treating its terminator as a root-object terminator shifts all table names
  onto the following body and corrupts texture/material/mesh associations.
- With the boundary corrected, basement decodes all 124 meshes and loads all
  36 requested textures. Verified mappings include `band_shadow.tex` to
  `band_shadow.png` and `ply_wood.mat` to `ramp_ply_mip.tex`.

## Open fidelity requirements

1. Prove the complete material state used by the basement; the current bright
   and white surfaces show that successful object decoding is not yet correct
   rendering.
2. Verify transform composition and draw membership against the authored room,
   including translucent ordering and culling.
3. Reproduce GH1 environment/light application rather than approximating it
   with GH2 defaults.
4. Reproduce `switch_cam` screen framing and target offsets from GH1 behavior.
5. Retain the verified Arena walk/stage helper placement contract while venue
   material, lighting, and camera behavior are corrected.
6. Repeat the verified pipeline on every GH1 venue before choosing native-read
   or offline full-file conversion as the shipping path.

## Venue-only validation mode

Run the application with `--venue-only`. It enables the existing venue-only
render path and suppresses the highway, gameplay HUD, performer models, and
performer props. This flag changes presentation only and never converts assets.

## Native `VenueCam::switch_cam` evidence

The GH1 executable dispatches the `switch_cam` symbol to the native handler at
`0x0016F3B8`. Its property-loading path establishes the following typed fields:

| Property | Native destination | Reader shape |
|---|---:|---|
| `singer_in` | `VenueCam + 0x1C` | two floats |
| `singer_out` | `VenueCam + 0x24` | two floats |
| `offset_in` | `VenueCam + 0x50` | vector reader |
| `offset_out` | `VenueCam + 0x60` | vector reader |

This disproves treating the singer pair and spatial offset as one generic
screen-offset operation. The implementation must preserve their separate
native roles. The bounded disassembly used for this recovery is stored at
`.codex/analysis/gh1_switch_cam_disasm.txt`.

The per-frame update at `0x0016E390` further establishes that:

- `singer_in/out` is interpolated from the selected path-frame progress and
  mapped from centered screen coordinates to viewport coordinates with
  `(x + 1) / 2` and `(1 - y) / 2`. The record target index selects the
  ArenaSinger subject; normal single-player index zero is the player
  guitarist.
- `offset_in/out` is interpolated separately as a three-dimensional framing
  offset.
- A record without an explicit `target` uses the selected parent transform as
  its starting framing point. Basement Intro has no target, so substituting a
  guitarist bone is incorrect.

The bounded update disassembly is stored at
`.codex/analysis/gh1_venuecam_update_disasm.txt`.

The decoded `venue.view` embedded transform is identity. Camera target
collection must preserve that transform separately from the centroid of the
View's 90 member meshes. Substituting the centroid visibly relocates the camera
and does not match native `SetTransParent` behavior.

The runtime performs that centered-to-viewport conversion explicitly. Basement
`INTRO/Intro01` maps `(0,.5)->(.5,.5)` to viewport
`(.5,.25)->(.75,.25)`. The local GH2 rotation helper accepts centered rather
than viewport coordinates, so the adapter converts back with
`x=2u-1, y=1-2v` at that API boundary. Passing viewport `u/v` directly to that
helper causes a second, erroneous half-screen displacement.

GH1 venue intro selection is distinct from GH2's CamShot/`Intro.tnm` route.
The legacy `camera.dtb` defines `$camedit.INTRO` as a `switch_cam` record that
selects a TransAnim from `venues/<venue>/gen/campaths.rnd_ps2`. Theatre selects
`Cam_t_np_zoom`, record name `Intro01`, path percentages 60 -> 0, and a 10000
ms real-time duration. The fourth atom names the record; it is not a target.
GH2 intro selection remains authoritative for GH2 assets; the DTB/campath
adapter is used only when a GH1 INTRO record exists.

All seven retail INTRO records explicitly use `duration 10000` and
`real_time 1`, so each is a ten-second / 300-camera-frame task. Their authored
path selections remain distinct:

| Venue | TransAnim | Path range |
|---|---|---:|
| `arena` | `Cam_t_np_zoom` | 0 -> 100% |
| `basement` | `Cam_basement_intro` | 0 -> 100% |
| `big_club` | `Cam_nt_np_arc_l` | 0 -> 48% |
| `fest` | `Cam_t_np_circle03` | 0 -> 70% |
| `small_club` / `small_club_multi` | `Cam_nt_np_zoom` | 0 -> 40% |
| `theatre` | `Cam_t_np_zoom` | 60 -> 0% |

INTRO and regular cameras share one GH1 `switch_cam` record decoder. This
keeps duration domain, framing, clip planes, DOF, crowd visibility, walking,
excitement, and character-LOD fields governed by the same source-format
contract. The complete decoded INTRO format and proofs are recorded in
`.codex/analysis/gh1-intro-venuecam-format.md` and
`.codex/current-evidence/gh1-intro-venuecam-proof/`.

Camera selection is only one half of intro fidelity. Native GH2 venues also
run authored venue-object animations during their intros. GH1 compatibility
must not bypass, replace, or globally disable that GH2 animation route; any
legacy venue-animation adapter must be selected by source format just like the
camera adapter.

VenueCam `start/end` values are path percentages rather than raw TransAnim
frames. Basement 0 -> 100 retains its full 88-key stair-descent path; theatre
60 -> 0 retains the first 60% (40 serialized samples plus an interpolated
boundary sample when needed) and traverses it in reverse. The current theatre
path therefore materializes 41 runtime keys. Treating 60 as a raw TransAnim
frame reduces that path to four keys and is a verified regression.
The camera solver must add `offset_in/out` after refreshing its native
frame-target cache; applying the offset earlier is incorrect because cache
refresh overwrites it.

Regular GH1 shots come from the remaining `$camedit.<category>` arrays in the
same `camera.dtb`. They feed the existing CameraManager category buckets;
the category names already match GH1 exactly. All seven GH1 venue directory
variants now materialize their available regular records. `big_club` is an
actual GH1 source directory and must remain distinct from GH2's `big` venue
alias.

Regular records use the same endpoint contract as INTRO. Their selected
TransAnim segment is retained as camera-position keys; `offset_in/out`,
`singer_in/out`, and `fov_in/out` are evaluated from the selected path frame
normalized between the lower and upper `start/end` frames. This is the exact
`SLUS-21224:0x0016E548..0x0016E5BC` branch: forward path traversal advances
`in` to `out`, reverse traversal advances `out` to `in`, and an equal
`start/end` range resolves immediately to `out` even when `duration` is
nonzero. A zero-duration record likewise resolves immediately to `out`.

Arena provides a direct retail proof. `flr_near_lft01x12w` selects
`Cam_t_np_close` with `duration 0`, `offset_in (0,0,0)`, and
`offset_out (-410,-110,-320)`. Keeping the `in` state places the eye inside
`drum_riser.1.mesh`; the renderer's source-ray inspector measures that
occluder only 7.3 units from the eye. Applying the zero-duration `out` state
restores the authored floor-left stage/crowd view without hiding that mesh.

GH1 `singer_in/out` coordinates are centered coordinates, not direct camera
translations and not vocalist selectors. Both intro and regular records retain
the interpolated `(x,y)` directly as GH2 CamShot screen offsets. Native
`VenueCam::Update` projects the selected ArenaSinger, measures its error from
that desired viewport point, and converts the error to camera-local right/up
translation. GH2 `CamShotFrame::Interp` performs the equivalent translation
from its centered `mScreenOffset`. Both preserve the authored camera
orientation; treating the value as a rotation rolls records with intentionally
off-screen coordinates.

The dedicated `small_club_multi` camera DTB has no GH2 normal-category
records. Its complete regular vocabulary is `MULTIPLAYER`, `MULTIPLAYER_0`,
and `MULTIPLAYER_1`, holding `multi01` through `multi03`. Those retail symbols
now participate in the legacy regular-camera category order. Before this
format bridge the selector found zero shots and exposed the raw rolled
`6 foot camera.cam`; the final unforced run selects `multi01` and is upright.
No venue-name or fixed-roll correction is involved. Full evidence and the
rejected raw-basis experiment are documented in
`.codex/analysis/gh1-venuecam-screen-translation-and-multiplayer-categories.md`.

The seven retail camera DTBs contain 201 regular records. Behavior-field
coverage is: `crowd_region` 194, `enable_dof` 162, `shaky` 141, `ease` 140,
`hide_crowd` 120, `real_time` 58, `walk_ok` 51, `force_cam_facing` 46,
`force_char_lod` 27, `eyes` 10, `guard` 3, and `low_excitement_ok` 1.
The extracted inventory is in
`.codex/current-evidence/gh1-camera-dtb-inventory/`.

Fields with an exact GH2 runtime counterpart now propagate from every regular
record: `hide_crowd`, `walk_ok`, `enable_dof`, `low_excitement_ok`, and
`force_char_lod`. Arena `flr_near_lft01x12w` carries `hide_crowd 1`; starting
that shot now hides the 15 decoded crowd meshes through the existing
CamShot `DoHide` lifecycle, and ending it restores them. It does not suppress
the arena's baked background imagery or hide unrelated venue geometry.
Cross-venue proofs are in
`.codex/current-evidence/gh1-regular-venuecam-fields-proof/`.

The 2026-09-01 live-camera acceptance pass closed three systemic integration
faults without altering any authored camera pose. First, the camera frustum
uses the active render output's width/height ratio, which is the target-side
equivalent of retail RndCam's renderer-derived Y ratio; the old fixed 16:9
assumption distorted 4:3 source framing. Second, when a converted GH1 venue
publishes its logical `Arena::walk_spots` vector, `CamShot::ShotOk` measures
the guitarist against that vector exclusively. Arena's current spot is
therefore `gh1_walk_spot_0`, not the generated GH2
`start_guitarist0.way` that happened to share its position. The exact authored
`bad_waypoints` entries on `flr_near_lft01x12w` and `x13w` now reject those
shots, after which normal category scanning selects `flr_near_lft02`.

Third, native GH2 CamShot parents are serialized ObjPtrs whose owners are
guaranteed by the loaded retail world and character packages. Loose DLC can
break that package invariant. Before a shot is accepted, every authored parent
used by its source and runtime frames must resolve from the assembled world or
performer graph. If not, the selector continues to the next authored candidate;
if a parent disappears after selection, submission retains the previous valid
camera. The original Fest failure was `flr_near_rt01`, whose local pose is
parented to `guitarist0:bone_neck.mesh`, combined with a requested GH80s
performer package that did not actually contain that model. Submitting that
local pose as world space produced the geometry-interior frames. With a valid
stock guitarist the same shot remains accepted and renders correctly; with the
deliberately missing performer it is rejected and the manager advances to
`flr_far_rt02`. This is a package-dependency check, not a geometry raycast,
camera offset, venue-name rule, character-name rule, or shot blacklist.

Final 4:3 native runs cover valid Fest, deliberately unresolved-parent Fest,
GH1 Arena, and GH1 Basement. Every retained runtime frame was inspected in
sequence; the accepted transitions remained unobstructed at 59.5-59.9 steady
FPS. The compact proof manifest and representative before/after frames are in
`proofs/venue-camera-item3/`.

The final translation/category regression set exercises both INTRO and regular
song time for every GH1 venue:
`.codex/current-evidence/gh1-venuecam-translation-category-final/`.

`real_time` selects the established RndAnimatable time domain. Records with
`real_time 1` use millisecond durations (commonly 10,000 or 19,000), converted
to 30 camera frames per second. Beat-timed records retain their serialized
tick count (commonly 7,680 or 11,520) and use the native 480 frames-per-beat
rate, so motion follows chart tempo. Arena proofs cover both domains:
`balcony_lft01` is 7,680 beat frames and `win01` is 10,000 milliseconds /
300 camera frames. Logs and moving-frame captures are in
`.codex/current-evidence/gh1-regular-venuecam-realtime-proof/`.

`ease` is a float interpolation parameter. Retail regular records serialize
95 zeros, 11 values of 0.5, and 34 values of 1. Native GH1 selects a
`LinearInterpolator` for zero and an `ATanInterpolator` for nonzero, passing
the serialized float as severity. Both INTRO and regular records use the
existing source-backed CamShot arctangent evaluator. Static and runtime proof
is in `.codex/analysis/gh1-venuecam-ease-static.md` and
`.codex/current-evidence/gh1-venuecam-ease-proof/`.

`shaky 1` selects the shared revision-4 `shaky_cam1.tnm` from
`../../system/run/arena/gen/fx.rnd_ps2`. Its 12 spline translation keys span
source frames 0–3200. Native `VenueCam` advances that animation from raw
elapsed task time and applies its translation additively to the active camera
path in the camera's local basis. The converter evaluates the same spline and
bakes the additive displacement into the native CamShot keys; a zero-duration
record still receives the complete shake interval through frame 3200. Static,
asset, and runtime evidence is in
`.codex/analysis/gh1-venuecam-shaky-static.md`.

`crowd_region` is an integer Crowd culling-region selector. Nonnegative values
select the zero-based authored region; `-1` asks native Crowd code to choose a
region from the camera/projection state. At venue construction, each original
`Crowd*.mm` instance is transformed into every
`crowd_limits%02d.mesh` local space and classified with an XY
point-in-triangle test plus the limit mesh's local-depth range. Switching
regions clears the active lists and copies the accepted original instances;
it never creates or modifies their transforms, materials, or animation.
The previous claim that target frustum/depth culling replaces this authored
camera-dependent draw-list reduction is **not parity evidence**. The 2026-09-03
camera audit found that `crowd_region` is retained in converted TypeProps but
not consumed by runtime. Theatre and Small Club native proofs still show
foreground crowd obstruction. Recover/reinstate the original region-selection
contract before accepting those views; do not hide arbitrary cards or reposition
individual venue cameras. The earlier static evidence was recorded in
`.codex/analysis/gh1-venuecam-crowd-region-static.md`.

All 41 packed GH1 `MultiMesh0` objects are crowd-card archetypes. Runtime now
decodes their template Mesh and exact ordered transform arrays, submits every
instance like `RndMultiMesh::DrawShowing`, and does not also draw the template
at its root transform. The converter emits
`__gh1_runtime_multimeshes.grp` as the native draw owner corresponding to
GH1 `Arena::Crowd`. After all venue sections merge, runtime binds the decoded
`crowd.env` to that Group. This follows the retail pointer stored at
`Arena + 0x9C` by `SLUS_212.24` `0x001685DC..0x0016862C`; it is not a
venue-name lighting rule.

The remaining low-frequency fields also have bounded native contracts:

- All 46 explicit `force_cam_facing` values and the default are zero, so the
  retail corpus contains no alternate behavior to translate.
- `eyes` is a four-mode performer eye-target controller: 0 disables targeting,
  1 targets the active camera, 2 targets forward from the character, and 3
  targets a periodically selected crowd member. VenueCam applies it only in
  the exactly-two-performer path, choosing player 0 or 1 from `Left`/`Right`
  camera-path prefixes. All ten explicit retail values are zero; the missing
  default is 3. It cannot affect the single-player converted venue path.
- `guard` appears only as `(0 9)` or `(0 0)` and feeds the PS2 raster
  guard-band globals. It does not alter the camera transform or projection
  geometry and has no target-platform render equivalent.

The converter preserves these source values in native flat CamShot TypeProps
for inspection and future two-player execution. They do not justify a
venue-, shot-, or asset-specific rendering rule. Eye-controller evidence is
in `.codex/analysis/gh1-venuecam-eyes-static.md`.

## Performer placement evidence boundary

GH1 venue directories do not provide GH2 `BandPlacer` objects or
`<venue>_chars.milo_ps2` assemblies. Theatre's revision-10
`venues/theatre/gen/theatre.rnd_ps2` contains 262 Mesh objects, 30 materials,
one camera, 33 Views/Groups, and **zero** standalone Trans or Waypoint objects.
Its authored View draw list includes `stage.mesh`, `stage.1.mesh`,
`stage.3.mesh`, and the `drum_riser*` aggregate, but those drawable bounds are
not serialized performer-role assignments.

Some GH1 venues contain a `characterLimits.mesh`. Its transform and vertices
are authoritative venue-space geometry, but the current quarter-grid mapping
of singer/guitarist/bassist/drummer roles is only a diagnostic A-pose
approximation. No decoded field or recovered native routine yet proves that
role ordering. Likewise, deriving role anchors from theatre's `stage.mesh`
bounds produces a visually coherent stage/camera composition, but is not a
shipping rule: the asset contains no relationship that assigns those sampled
points to performers.

Do not add venue-name coordinates, mesh-name placement tables, or per-venue
camera compensation. Recover the common GH1 placement contract from native
code or PCSX2 traces. Useful trace evidence must capture the final world
transforms assigned to the four band directories and the source object or
script state used to choose them. Until that evidence exists, A-pose placement
proofs are explicitly provisional and cannot establish completed character or
camera fidelity.

The shared `charsys/gen/charsys.dtb` supplies a separate `band_spots` table:
`bass -> 1`, `drummer -> 2`, `keyboard -> 0`, and `singer -> 0`. These integers
are authored logical spot selections, not coordinates. Theatre's venue script
also calls `{char_sys get_spot guitarist0}` when driving
`spotlight01.tnm`.

Static analysis of the retail GH1 ELF establishes the native representation.
CharSys owns a contiguous runtime spot array whose records are `0x40` bytes;
the begin/end pointers are stored at `0x00363838`/`0x0036383C`. Native code
bounds-checks a `band_spots` result against that array, indexes it with
`index << 6`, and applies the selected record to a character. Record offset
`+0x30` is the position vector. `CharSys::get_spot` at `0x0018EF80` performs
the complementary query: it compares the character's current world position
with every record's `+0x30` vector and returns the nearest record index.
Therefore `get_spot` confirms an already-applied logical spot; it is not the
source of venue placement.

Static Arena initialization supplies the source-backed asset contract: GH1
probes sequential Mesh objects named `stage_spot_%02d.mesh` and
copies/composes their authored transforms into `0x40`-byte records. The Arena
object stores this stage-spot vector at `+0xE0`; the same generic routine
populates `walk_spot_%02d.mesh` records at `+0xF0`. The embedded assertion
text, "This venue requires stage spots for the band!", confirms that these
helpers are required venue data rather than optional editor decoration.

This supersedes any stage-bounds or `characterLimits.mesh` placement
hypothesis.

Theatre exposes a section boundary: its main revision-10 RndDir reports all
396 decoded objects (262 Meshes) but no `stage_spot` or `walk_spot` names.
Those objects are not missing from retail data. Theatre's script first
executes `{arena load_section lighting lighting}`, and
`venues/theatre/gen/lighting.rnd_ps2` contains all six helpers:
`stage_spot_01..03.mesh` and `walk_spot_01..03.mesh`. Runtime section loading
therefore owns both visible lighting and non-draw Arena placement data.

The normal GH1 four-character mapping is fully source-backed: guitarist0 uses
the zero-initialized walk selector and therefore `walk_spot_01.mesh`; singer
uses stage index 0, bass stage index 1, and drummer stage index 2. The helper
Mesh translation and axis directions define placement. Its basis magnitude
sizes the visible editor box and does not scale the character. Theatre proof
resolves the resulting positions to `(141.0,460.4,-15.7)`,
`(18.1,437.3,-16.1)`, `(-183.3,475.3,-16.1)`, and
`(-3.8,598.3,3.5)` respectively.

The runtime now enumerates these numbered helpers generically in every loaded
scene, merges lighting-section transforms into Arena lookup, and applies the
shared CharSys mapping. The former `characterLimits.mesh` quarter-grid and
basement `riser.mesh` fallbacks were removed. There are no venue names,
sampled stage bounds, or per-venue coordinates in this path. The bounded
address-level record is
`.codex/analysis/gh1-charsys-band-spots-static.md`.

Retail inventory verifies this contract across the complete seven-directory
GH1 venue set. Basement, big_club, fest, small_club, small_club_multi, and
theatre each carry stage spots 01-03 in their lighting RndDir and at least two
walk spots. Arena carries stage spots 01-03 and walk spots 01-04 in its main
RndDir instead; generic scene enumeration covers both section layouts.
Independent small_club runtime proof resolves all four roles through those
helpers and exits successfully with highway and HUD/meters disabled.

A read-only retail Basement state closes the static-placement side of this
contract. Arena global state `0x00363748 + 0xE0` holds three live stage
records whose `+0x30` positions exactly match the extracted
`stage_spot_01..03.mesh` translations. The vector at `+0xF0` holds two live
walk records that exactly match `walk_spot_01..02.mesh`. The converted
`start_singer.way` retains the same `stage_spot_01.mesh` translation as the
retail stage record. A later live singer-bone position cannot be compared to
that waypoint without matching the performance-animation state; it does not
justify camera or placement compensation.

## Legacy ObjectDir draw roots

GH1 nested venue directories draw through an authored root `View` named after
the directory (`lighting.rnd_ps2` -> `lighting.view`). They may also contain
unreferenced editor/helper geometry. In big_club, every
`crowd_limits*.mesh` lies outside the authored View hierarchy, whereas the
real beam geometry is explicitly reached through `lighting.view` ->
`beam.view`. Rendering all ungrouped meshes as roots therefore exposes helper
geometry that the game never draws. Revision-10 directories now select
`venue.view` when present, otherwise their filename-matched root View.

View7 also carries a preliminary animation/message-propagation member list
before its embedded transform and drawable list. It is not a second draw list.
For example, big_club `lighting.view` propagates to `verse.anim`,
`chorus.anim`, `beatOK.anim`, and `spotlight01.tnm`. The decoder preserves
these as `GroupObj::anim_children`, and runtime switch/range propagation now
feeds the TransAnim, MeshAnim, LightAnim, and EnvAnim samplers.

Both member lists belong to the View on which they are serialized.
`children_owner` is retained as an independent legacy owner/editor link and
must not redirect either list. Theatre proves the distinction: `verse.anim`
points at `chorus.anim` as owner but carries its own
`verselight01/02/03.envanim` members. The former owner-following conversion
therefore drove verse lighting with chorus tracks. The converter, its audit,
and a distinct-owner regression now consume the source View's own lists. A
fresh complete rebuild passes 678/678 View mappings, 105/105 converted assets,
and all seven venue camera/script/package audits; see
`proofs/manage-band-20260906/gh1-view-member-regeneration-v4/`.

Renderer traversal now expands nested View/Group children recursively from
the selected revision-10 root and does not append ungrouped Meshes afterward.
This matches ObjectDir draw ownership and prevents placement, crowd-limit, and
other editor helpers from becoming implicit render roots. Numbered Arena
stage/walk/fire/name-light spot Meshes are also classified explicitly as
non-draw runtime helpers in both main and section RndDirs.

## Ordered View environment scopes

A GH1 revision-7 View may change rendering environment inside its ordered
Drawable list. Retail Small Club `lighting_transparent.view` begins under
`Environ2`, draws its particles and `lighting_rig.view`, then switches to
`smoke.env` before the four smoke planes, scaffold Views, and solo View. The
nested lighting rig independently switches through orange, pink, and red
Environs. Selecting the first Environ as one native Group environment therefore
changes real source state and incorrectly lights later drawables.

The converter now resolves the ordered stream into environment segments. A
single-scope View keeps the ordinary native Group environment. A multi-scope
View emits deterministic `<view>.__environment_<index>.grp` children, each
with the exact source environment and ordered drawable members, plus a
`<view>.__draw_only.grp` traversal container. Animation/message membership
remains on the converted parent Group. The flat View conversion helper rejects
multi-scope input instead of losing state.

Focused Small Club conversion proves `lighting_transparent.view` emits
`Environ2` and `smoke.env` segments and `lighting_rig.view` emits its three
color scopes. The complete audit converts 105/105 MILO assets and 926/926 ACPs
with zero blockers. In the deployed archive,
`smoke_plane01.mesh` now resolves through `smoke.env` with ambient
`(0.3, 0.2, 0.15)`. Source and converted `smoke.tex` decode to the same RGBA
SHA-256, `4b65c5d20601268485446b1699d622d79827495e81a76a357ba9e7a0534cae90`.
A deterministic A/B confirms the plane is the textured golden haze, not the
solid red prop block. Evidence:
`proofs/gh1-native-conversion-parity/view-environment-scope-contract/`.

## Revision-21 material texture entries

GH1 `RndMat` revision 21 begins with a variable texture-entry array. Each
entry carries two selectors, a 12-float texture matrix, wrap state, and a
texture reference. Selector 0 is not the only sampled 2-D texture family.
Retail small_club proves selector 1 on `smokemat`, `color_plane.mat`, and
`spot_beam_mat`; all three otherwise become untextured additive polygons.
Selector 5 entries reference sphere/environment textures.

The reader now accepts the first non-empty selector-0/1 entry as the material's
2-D texture and retains selector 5 as environment data. Small-club consequently
loads 13/13 lighting textures instead of 9/9, and its former large flat
red/purple surfaces resolve to authored neon and textured translucent content.
Evidence is in
`.codex/current-evidence/gh1-small-club-mat21-selector1/`.

## Arena-consumed helper Mesh lifecycle

GH1 does not draw every Mesh object stored in an Arena directory. The retail
ELF contains `arena::target_parent.mesh` at `0x00311188`; its xref at
`0x0016DDB0` resolves the object and passes it to `0x00167E28`. That routine
removes references to the object from its reference containers, then invokes
its virtual destructor with delete flags `3`. `target_parent.mesh` is therefore
temporary Arena transform/runtime data, not visible venue geometry.

The same native Arena naming contract includes zero-based
`crowd_limits%02d.mesh`, while stage, walk, fire, and name-light spot families
are sequential runtime helpers. The renderer classifies these families
generically in both the main directory and loaded section directories. This is
not keyed to small_club or to a corrective visual heuristic.

With the native lifecycle applied, the solid red `target_parent.mesh` block is
absent while authored geometry and helper-derived performer placement remain
unchanged. The 2026-07-28 regression was caused by the already recovered helper
classifier being dropped from both venue and lighting base-hidden sets; the
generic classifier is restored rather than replaced with a Small Club fix.
The exact same fixed camera now retains the complete amp and authored smoke
haze while removing only the temporary helper. A fresh strict-native
seven-venue sweep reaches gameplay at 57.269--57.730 steady FPS. Evidence:
`proofs/gh1-native-conversion-parity/view-environment-scope-contract/` and
`proofs/gh1-native-conversion-parity/venue-parity-matrix/final-view-scope-helper-lifecycle/`.

## GH1 lighting messages, function namespaces, and `OFF`

GH1 performer lighting environments are owned by the lighting RndDir, not a
GH2-style venue-character proxy. Every venue provides `stagechar.env` and
singer Environs; venue functions dynamically call
`arena::set_singer_env <singerN.env>`. The runtime retains that operation,
applies the selected singer environment to the singer, and applies
`stagechar.env` to the remaining roles through the RndDir which owns those
objects. Small Club Multi has no singer selector and uses its shared
`stagechar.env` for all roles. The complete source inventory and seven-venue
proof are recorded in
`.codex/analysis/gh1-performer-lighting-environment-format.md`.

GH1 venue DTBs define lighting functions separately from Arena message
handlers. The names intentionally overlap: an Arena handler such as
`set_lights_bad` calls the function named `set_lights_bad`. Keeping both in one
map overwrites the function with its wrapper and turns the call into recursion.
The loader now retains distinct function and message-handler namespaces.

The shared `OFF` macro expands to:

```text
((loop 99999 99999) (scale 1) (blend 0))
```

Macro substitution retains the outer option array. `switch_anim` therefore
accepts both inline option rows and bundled option arrays. A degenerate
loop/range is a static `SetFrame` state rather than a request to hide a mesh.

Revision-7 GH1 Views have a preliminary Animatable propagation vector before
their embedded transform and drawable members. Small-club `solo.anim` contains
`spotlight.litanim` and `solo.envanim` in this vector. Its retail
`set_lights_bad` function sends `solo.anim OFF`; frame `99999` makes
`solo.envanim` drive `solo.env` to its authored off color. Theatre uses the
same contract with `sololight01.envanim` and `sololight02.envanim`.
Nondegenerate View animation ranges propagate to both `EnvAnim` and `LightAnim`
members with their authored loop/range, scale, and blend values; they are not
reduced to the static `OFF` case.

Animatable0 scale/range records are frame filters, not self-starting clocks.
Retail `SLUS_212.24:0x001AC0C0` loads the operation filters and child
Animatables, `0x001AC640` creates operation types 0 through 4, and
`0x001ABAB8` filters only a frame supplied by an external caller before
recursing to children. No task starts in those paths. Consequently the
unowned Small Club `smoke.matanim`, `record.matanim`, and
`record_g.matanim`, and Theatre `tram.mnm`, must not be advanced from an
invented song-time clock. Their native MatAnim/filter pairs preserve callable
source behavior without fabricating playback. The complete ownership and
static execution trace is in
`.codex/analysis/gh1-native-matanim-scheduling/`.

GH2 gameplay excitement and chart-section state are translated to GH1's
native message family:

- bad -> `set_lights_bad`
- okay/great + verse, chorus, or solo ->
  `set_lights_<okay|great>_<section>`

This translation invokes retail venue functions; it does not tune fixture
brightness, suppress `light_solo_opt.mesh`, or replace `kAdd`. The
`spot_beam.tex` used by that mesh is fully opaque, and draw-order evidence
shows the mesh is submitted once. HUD/highway-free proofs are in
`.codex/current-evidence/gh1-native-lighting-message-proofs-v2/`.

An authored revision-1 `RndEnviron` with no Light references is ambient-only.
It must not inherit the renderer's diagnostic fill lights. Theatre's
`verse/chorus/solo` Environs use exactly this shape to color additive
`light_spot_*.mesh` pools through `RndMat::use_environ`. Supplying fallback
directional lights in addition to the animated ambient color caused the pools
to saturate the stage white. The renderer now installs defaults only when no
Environ owns the mesh; zero authored lights disables the fallback slots.
Format and isolation evidence are in
`.codex/analysis/gh1-rndenviron-zero-light-state.md`.

That propagation now feeds the existing venue TransAnim/MeshAnim sampler.
Legacy `.view` and `.anim` names are animation groups alongside `.grp`, and
the lighting subdirectory retains its direct animation routes. GH1
`venues/<venue>/gen/<venue>.dtb` handlers are decoded separately from GH2's
`world/<venue>/gen/<venue>.dtb` schema. `arena switch_anim` supports authored
`loop`/`range`, `scale`, and millisecond `blend` rows, including scale-only
updates that preserve the current range. Direct `set_showing` commands resolve
against the venue or lighting View hierarchy.

GH1 also serializes the distinct Arena operation `switch_anim_rt` with the
same option payload. Retail occurrence counts are arena 25, basement 48,
festival 1, and theatre 6. It is now decoded by the same reference-driven
animation route while retaining an `anim_realtime` flag; this restores
basement's otherwise-empty `anim_great` function and the handlers which call
it. This is opcode-derived behavior, not selection by venue or object name.
The global `animate_to` task is now decoded generically. Retail calls preserve
their named View, destination frame, and millisecond period; the runtime
propagates the transition to the View's authored animation members. This
covers Theatre's `main_curtains.view` and Festival's `reactor_set.view`
without venue or object-name exceptions. The source-format record is
`.codex/analysis/gh1-venue-script-animate-to.md`.

Venue animation is an explicit completion gate. The first deterministic audit
inventories all seven DTB handler/function sets and decoded animation objects,
then proves active `set_lights_great_verse` routing and time-separated rendered
changes with fixed cameras and all gameplay presentation hidden. This proves
the runtime is not globally static, but exact retail timing and state-transition
coverage remain required. The gate, counts, evidence, and remaining acceptance
work are recorded in
`.codex/analysis/gh1-venue-animation-completion-gate.md`.

GH1 expects the lighting section to exist before `intro_start` handlers run.
Because the local assembly initializes the main venue first, legacy intro,
music, and current excitement script messages are replayed once after the
lighting directory is ready. This replay is GH1-gated and does not alter GH2's
intro/event route.

GH1 Mesh25 embeds Drawable revision 1, whose legacy drawable list is
load-bearing scene structure rather than disposable metadata. Aggregate meshes
such as `main_hall.mesh` enumerate their material-split children
`main_hall.1.mesh` through `main_hall.5.mesh`. The decoder now preserves this
list as `MeshObj::drawable_children`, and authored View traversal recursively
emits those children. Big-club's main draw list increases from 107 to 153 of
159 decoded meshes; the remaining six stay outside the authored hierarchy.
This restores the previously absent multi-material hall, rail, wall, and rug
sections without reintroducing unreferenced helper geometry.

Drawable1 aggregate roots also expose a narrow Trans8 cache distinction. Their
serialized world matrix can be stale even though the root has no transform
parent; native `WorldXfm_Force` rebuilds that root from local before listed
drawable children inherit from it. Revision-10 meshes with authored
`drawable_children` now use the parent-composed local result. Their children
continue using the serialized world cache. Applying local composition to every
GH1 mesh is incorrect and moves theatre's shell across its camera path. This
narrow rule reconnects big-club's hall sections while preserving theatre.

## ParticleSys revision 22

All 76 ParticleSys objects in the packed GH1 archive use revision 22. Its
source order is Animatable0 legacy range/reference lists, Trans8 legacy
children, Drawable1, and the particle payload. Arena's `stage_flame` objects
prove the Animatable lists are variable-length rather than fixed padding.

The exact sequence after the four start/end colors is a one-byte
`bounceEnabled`, a four-float plane, a three-float force direction, and the
material reference. The earlier apparent optional prefix and three-float
variant were a misaligned interpretation of that plane. There are no
asset-specific body shapes.

After type, grow/shrink/mid-color state, particle limit, and bubble state, the
revision-22 tail stores relative motion, an emitter Mesh reference, and
preserve-particles state. Preserved particles use a 32-byte row: position
Vector3, color as four floats, and size. Five packed objects carry non-empty
preserved vectors and independently prove the stride.

The semantic reader/writer round-trips all 76/76 packed bodies byte-exactly,
including empty-material Big Club fog systems and the festival
`nuke_toxic` system, with zero residual bytes. The runtime proof logs for the
original four-venue subset remain in
`.codex/current-evidence/gh1-particle-rev22-runtime/`; proof frames are in
`.codex/current-evidence/gh1-particle-rev22-proofs/`.

GH1 VenueCam intro records use the same offset convention as regular camera
records: `offset_in/out` is an interpolated world-space correction to the
moving TransAnim eye, not an offset added to the look-at target. Applying it
to the target aimed the RedOctane Club intro into the upper hall. Applying it
to the eye restores the source-authored wide stage reveal while the venue
intro handlers and object animation run concurrently. Retail GH1 footage is
the visual baseline; the remaining late-intro transition into the first
regular shot still needs exact timing validation.
## Revision-10 Mat21 material state

The apparent Mat-to-Mesh misalignment was stale-tool output. A rebuilt
`milo_tool` proves normal last-Mat and first-Mesh bodies in all seven venue
ObjectDirs and the sampled Metal character ObjectDir. Theatre’s declared
`19 - Default.mat` correctly contains `band_shadow.tex`.

Mat21 is now decoded in the exact native retail `RndMat::Load` order recovered
from `SLUS_212.24` at `0x001BE900`. Each legacy texture stage is
`blend`, `tex_gen`, a 12-float transform, `tex_wrap`, and a texture reference.
The stage vector is followed by overall framebuffer `blend`, RGBA color,
`useEnviron`, `vertexAmbient`, `vertexDynamic`, `cull`, `multipass`,
`normalize`, `zMode`, `alphaCutout`, and `alphaWrite`. The retail debug routine
at `0x001BE458` independently names those fields and the stage `blend` and
`genMode` values.

The compatibility renderer maps GH1 `vertexAmbient` to GH2 `prelit`, supported
by the shared GH1/GH2 material corpus, while retaining `vertexDynamic`
separately. An environment-map stage does not override the authored
`useEnviron` flag. No material-name or venue-name rule is involved.

Extracted Milo `.tex` files can now be decoded by `tex_tool`. The theatre
shadow texture proves that PS2 texture output is straight-alpha RGBA. Blend 2
therefore uses `SRCALPHA/ONE` instead of premultiplied-only `ONE/ONE`, removing
the transparent white floor rectangle generically for additive GH1 materials.

The complete Mat21 body layout now round-trips all 1,693 packed materials
byte-exactly, and every serialized field has a source-backed semantic name.
GH1 multi-stage/multipass materials become deterministic GH2 revision-27
`nextPass` chains while retaining stage blend, TexGen, texture transform,
wrap, Z, alpha, color, environment, and cull state. Cross-archive
measurements, native disassembly, negative experiments, and palette
statistics are retained under `.codex/analysis/`.

## View drawable boundaries and Small Club hanging records

GH1 revision-7 `View` has two independent membership channels: its legacy
Drawable child list controls submission, while its animation/message list
controls propagation. GH2 revision-13 `Group` combines object membership but
adds a `draw_only` Group reference. The converter now uses that native field
instead of treating every animation target as drawable. It also recursively
expands revision-1 Drawable child lists and retains nested Views as authored
draw boundaries. Unreachable editor/helper drawables remain addressable in
the target RndDir but are held by a hidden native Group so RndDir root
discovery does not submit them.

This maps Small Club's raw and converted graphs exactly: the main directory
submits 181 meshes from 211 grouped objects in both paths, while lighting
submits 145 meshes from 241 grouped objects in both paths. The additional
target Groups are only deterministic `draw_only` companions and the hidden
unreachable-object owner.

The hanging records are not opaque cards in the source. `record.mat`,
`record_g.mat`, and `record_r.mat` all decode as `kBlendSrc`, normal Z,
`alphaCut=true`, `alphaWrite=false`, and `cull=false`. The final field makes
the authored cards two-sided. Their three 64x64, 8-bpp indexed PS2 bitmaps
each decode to 814 transparent pixels, 130 partial-alpha pixels, and 3,152
opaque pixels. Raw and converted decoded images are byte-identical.

The paired `record*.1.mesh` objects are not rear cards. All 21 attached
instances are source-authored four-vertex/two-face suspension strips parented
to the corresponding record. Their invariant long extent is 44.683188 source
units and their middle extent is only 0.201836--0.281165 units. A complete
raw-to-native sweep reports zero vertex, normal, shared color-slot, UV, face,
or material mismatches for these strips.

The venue renderer previously ignored decoded Mat alpha-cut state. It now
sets alpha-test enable, `D3DCMP_GREATER`, and the decoded/defaulted material
threshold on every mesh draw. Mat21 and retail PS2 Mat27 do not serialize a
threshold, so their recovered default is zero. This leaves only the authored
record silhouette without a black-key, record/material/mesh name, or venue
exception. Full evidence is in
`proofs/gh1-native-conversion-parity/venue-prop-layering-contract/`.

The shared renderer correction also passes a hidden, input-free 15-venue
regression: all seven converted GH1 venues and all eight native GH2 venues
load, render fixed-time frames 2 and 120, and exit zero with no failed load,
unsupported operation, or unresolved target/reference. This proves the GH1
material correction does not replace or bypass the GH2 venue path. The matrix
and contact sheets are in
`proofs/gh1-native-conversion-parity/shared-alpha-cut-regression/`; it remains
a regression smoke test, not matched retail parity.

The separate crowd-card alpha audit covers all eight GH1 packages containing
MultiMeshes. Seven share a 128x256 indexed image with 25,220 transparent and
7,548 opaque pixels. Theatre has the same RGB pixels but its source CLUT marks
all 32,768 pixels opaque; its `crowd.mat` also authors SrcAlpha,
`useEnviron=true`, `cull=true`, `zMode=1`, `alphaCut=false`, and
`alphaWrite=false`. Raw and converted bitmap digests match in both cases.
Retail tracing proves that 8-bpp textures consume CLUT alpha and that
Theatre does not take the `_tb` bitmap-conversion branch. The runtime therefore
does not infer transparency from opaque indexed storage globally. Instead, it
reconstructs the exact sibling binary mask only for a fully opaque image used
by an alpha-blended `MultiMesh0` template. This source-topology rule has no
venue, texture, material, or mesh-name condition; the fresh Theatre proof
submits 500/500 cards with 25,220 transparent pixels and no rectangular
backgrounds. Full crowd, floor, environment, and 15-venue evidence is in
`docs/CROWD_AND_FLOOR_SOURCE_PARITY.md` and
`proofs/gh1-native-conversion-parity/crowd-floor-source-contract/`.

## Resolved proof observations

- The GH1 singer's microphone stand was upside down in earlier integrated venue
  proofs. This venue/performer-prop observation is now resolved by loading the
  retail role-owned singer ACP, not by changing the stand or venue transform.
- The earlier missing/backface-inverted singer face and displaced Metal wrist
  are closed by the complete face topology, skin palette, and authored
  twist-controller conversion. They are covered by the all-GH1 character
  proofs rather than venue-specific corrections.
- No object-name hide, per-character vertex edit, or venue coordinate override
  is authorized by these observations. Corrections require native hierarchy,
  winding, or transform evidence.

Focused mic evidence shows that `mic_stand.mesh` is an embedded rigid singer
mesh parented to `bone_pos_mic.mesh`, not venue geometry or an external prop.
The GH1 and GH2 Metal singer archives contain identical mesh bounds and local/
stored transform rows. The earlier viewer/gameplay difference came from the
viewer applying a clip while gameplay had failed to resolve the role-owned
`singer_idle.acp` and `singer_active_medium.acp` filenames. With those retail
clips loaded, the authored parent chain places the stand upright across all
seven venues. See `.codex/analysis/gh1-mic-stand-transform.md`. No mic-specific
rotation or Arena-basis change was added.

Small-club's large pale panels have also been identified rather than patched:
the picked retail object is `main_room_stage.1.mesh`, using
`plaster_wall.mat -> plaster.tex`. The embedded 128x128 texture is itself pale
chipped plaster, and its Mat21 state decodes normally as prelit, environment-
enabled, normal-Z, opaque source blend. Treat the prominence of those panels as
awaiting native comparison, not as evidence of a missing texture. See
`.codex/analysis/gh1-small-club-white-panels.md`.

## Prelit and environment light-channel contract

GH2 PS2 `Ps2Mat::Select` at `0x0019CFE0` computes its material light-channel
count at `0x0019D12C..0x0019D154`. It reads `mUseEnviron` at `+0x40` and
`mPreLit` at `+0x9C`, then enables one light channel exactly when
`mUseEnviron || !mPreLit`. The independent Wii renderer signature names the
same local `numLightChannels`.

This distinction matters to both source families. A prelit material without
an environment is already lit and bypasses fixed lighting. A prelit material
which opts into an environment still consumes authored ambient, approximate
and dynamic lights; its baked vertex color is the material input to that
channel. The former PC renderer bypassed all prelit materials and manually
folded in ambient only, making common `prelit=true, use_environ=true` Basement
surfaces nearly black.

The renderer now consumes only those two decoded fields. Converted GH1
Basement retains the animated blue environment while restoring its plywood,
rugs, walls, appliances, drum area, and performers. Native GH2 Arena retains
its authored cyan/green environment. Evidence:
`proofs/gh1-native-conversion-parity/material-light-channel-contract/`.

## Full GH1/GH2 frame-pacing contract

The renderer and Windows frame scheduler now pass a hidden, input-free,
strict-native performance matrix across all seven converted GH1 venues and
all eight native GH2 venues. Every row reaches gameplay with four
GH2-layout performers, produces four gameplay profile windows, and reports
zero gameplay-time initialization. Steady cadence is 58.785--60.201 FPS
(16.611--17.011 ms); sampled D3D9 presentation remains 0.241--0.452 ms.

The original 33.241--58.242 FPS spread was not a GH1-format cost. D3D9
presentation waited for refresh and the application then applied another
frame deadline. Presentation is now immediate and one accumulated application
deadline owns the 60 Hz cadence. Shared renderer improvements cache immutable
buffers only for provably static meshes, cache authored parent lookup/base
world transforms, and reuse consecutive identical environment state.
Disconnected XInput slots are probed periodically while connected devices
remain polled every frame. No venue, mesh, material, object, or character-name
case was introduced, and native GH2 venues use the same path without being
clobbered. Full results and the reproducible runner are in
`proofs/gh1-native-conversion-parity/frame-pacing-full-matrix/`.
