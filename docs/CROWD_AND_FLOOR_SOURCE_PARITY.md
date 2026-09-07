# Crowd and Crowd-Floor Source Parity

## Release crowd quality policy

Camera-selected near and semi-close crowd regions must always use full 3-D
characters. Their legacy flat cards are removed before replacement rendering
and are never a missing-asset or low-fullness fallback. If a required 3-D
actor cannot load, the selected card remains suppressed and runtime emits a
release-invariant error instead of putting a 2-D person into a close view.

Flat crowd cards are permitted only for non-selected, far-distance regions.
The `GHOGX_DIAGNOSTIC_GH1_CROWD_3D_ONLY` capture switch may suppress even those
distant cards, but it is proof tooling rather than the production policy.

## Later camera-audit findings (2026-09-05)

The acceptance below is historical, not a claim that every current crowd view
is correct. The camera audit subsequently fixed duplicate GH1 world/lighting
crowd draws, then used a saved retail Basement scene to correct archetype
ordering, promotion-plane height, one numerically sensitive region member,
and SetSizes retaining the wrong half of the crowd. All 12 saved regions,
all 92 instance origins, and all 36 live flat cards now match that oracle.
Big Club's remaining foreground obstruction still needs a matched source check.
See `CAMERA_DRIVER_AUDIT.md` for current evidence and remaining limitations.

## Scope and result

This work closes the crowd and crowd-floor rendering subcase for all seven
converted Guitar Hero 1 venues and all eight native Guitar Hero 2 venues. It
does not claim full matched venue-presentation parity.

The implementation now has two deliberately separate source paths:

1. GH1 `MultiMesh0` crowd cards are decoded, owned, transformed, shaded, and
   submitted as authored.
2. GH2 `WorldCrowd6` actors and placement meshes retain the native GH2 path.

No venue, mesh, material, or texture name is used to invent visibility,
flooring, alpha, camera-facing behavior, or placement.

## GH1 retail contract

### MultiMesh drawing

All 41 `MultiMesh0` objects in the packed GH1 corpus are crowd-card
archetypes. A `MultiMesh0` contains a Drawable block, a template Mesh
reference, and an ordered array of serialized instance transforms.

The target renderer follows `RndMultiMesh::DrawShowing`: for each source
instance it installs the serialized transform on the referenced template Mesh
and draws that Mesh. The template is not also submitted once at its root
transform. No synthetic camera-facing transform, foreground cull, or guessed
bounds test is applied.

GH1 `Arena::Crowd` owns these MultiMeshes independently of the current View
drawable list. The converter therefore emits a deterministic native owner
Group named `__gh1_runtime_multimeshes.grp`. That Group keeps source order and
contains the MultiMesh references; it is not a venue-specific visibility
exception.

### Crowd region

Retail GH1's `crowd_region` selects a subset of the original MultiMesh
instances. Construction classifies each authored instance against the
`crowd_limitsNN.mesh` volumes; changing region copies accepted original
instances into an active list. It does not modify transforms, materials, or
animation.

The target keeps the complete serialized instance arrays and lets ordinary
frustum and depth behavior reject unseen cards. This preserves all source
content without reproducing a redundant retail draw-list optimization.

### Environment ownership

GH1 retail stores `arena::crowd.env` at `Arena + 0x9C`. Static evidence is
`SLUS_212.24` `0x001685DC..0x0016862C`; the pointer is subsequently read from
that field by the crowd path.

Converted crowd packages cannot serialize an unresolved cross-directory
ObjectPtr to an environment held by another venue section. After all native
venue sections have merged, runtime resolves the decoded `crowd.env` and
assigns it to `__gh1_runtime_multimeshes.grp`. Normal Group traversal then
propagates that environment to each MultiMesh template Mesh. This is a
source-ownership bridge, not a brightness correction.

### Texture alpha

The eight GH1 packages containing MultiMeshes were audited from the source
MILO and again after conversion.

- Seven packages use the same 128x256 indexed crowd image. It decodes to
  25,220 transparent pixels and 7,548 opaque pixels, with no partial-alpha
  pixels. Source and converted bitmap payloads match.
- Theatre's same-sized `crowd01.tex` has the same RGB pixels but its source
  CLUT marks all 32,768 pixels opaque. Its source `crowd.mat` uses SrcAlpha,
  `useEnviron=true`, `cull=true`, `zMode=1`, `alphaCut=false`, and
  `alphaWrite=false`. The converted bitmap digest remains identical to the
  source digest.

Retail `RndTex` tracing finds no missing serialized flag: the `_tb` filename
branch calls `RndBitmap::SetAlpha(kTransparentBlack)` at
`SLUS_212.24:0x001B02EC..0x001B0308`, but Theatre's path has no `_tb`.
`PsTex::Sync` maps 4/8/16/24-bpp formats at
`0x001A14B4..0x001A14E8`, and its normal upload path sets TEX0.TCC from
`bpp != 24` at `0x001A1C38..0x001A1C68`. The 8-bpp Theatre image therefore
uses its CLUT alpha in retail; an all-opaque indexed palette is not a generic
alpha-less-texture flag.

The source corpus nevertheless establishes the missing silhouette without a
venue or asset-name exception. Every audited `MultiMesh0` is a crowd card;
the seven sibling packages carry the exact binary alpha mask, and Theatre's
decoded RGB pixels match them byte-for-byte. Runtime applies that established
mask only when a decoded, alpha-blended MultiMesh template has a fully opaque
image. All other indexed textures retain authored CLUT alpha. Theatre then
decodes to 25,220 transparent and 7,548 opaque pixels, matching the other GH1
crowd cards.

## GH2 retail contract

Native GH2 uses `WorldCrowd6`, not GH1 MultiMesh crowds. The decoded
WorldCrowd owns character billboards, an authored placement Mesh, count, and
environment.

`WorldCrowd::BuildBillboard` emits faces `(0,1,2)` and `(1,3,2)`. The D3D
bridge must preserve that source front face through its mirrored clip-X
projection. Using `D3DCULL_CCW` rejected every live billboard even while the
draw ledger counted every submission. The bridge now uses the same
`D3DCULL_CW` front-face mapping as the normal character path. A paired
source-enabled/source-disabled capture established that the old frames were
byte-identical; the corrected frame visibly contains the decoded actors.

`WorldCrowd::BuildBillboard` constructs the four-vertex character billboard.
The placement Mesh supplies positions; it is not a visible floor surface.
`WorldCrowd::CleanUpCrowdFloor` discards placement-mesh vertex data outside
the editor after crowd construction. The compatibility runtime therefore
never renders those placement meshes as flooring and never substitutes a
guessed `tile_dark.mat` or `street_asphalt.mat`.

Venue floors remain ordinary authored venue Mesh objects. The previous
diagnostic path that manufactured visible floor geometry from crowd
placements has been removed.

## Animation contract

GH2 WorldCrowd animation remains data-driven. The retail
`world/crowd.dta` event mapping selects fullness and clip groups; the proof
uses `excitement_peak`, which selects full 4:3 placement coverage and the
source `great` group. A diagnostic-only logger records actor name, actor MILO,
clip name, clip source MILO, source beat/time, channel count, and a
deterministic FNV-1a digest over every sampled `ClipChannel`. Across all eight
native venues, all 49 actors change digest during the five-frame proof
interval, with at least 19 decoded channels per actor.

GH1 MultiMesh cards are source-static. `MultiMesh0` carries Drawable state,
the template Mesh reference, and transforms, but no Animatable state. The
full source audit finds no animation object targeting a crowd MultiMesh, its
template Mesh, or `crowd.mat`. GH1's global crowd character ACP files are
therefore not attached to venue cards. Crowd-related venue `LightAnim`
objects remain decoded and routed normally: 24 source rows across the seven
converted venues, with Theatre's three rows holding one constant key each.

## Implementation points

- `milo_scene` decodes exact `MultiMesh0` template references and transforms.
- `milo_convert` emits the deterministic GH1 runtime owner Group and prevents
  its MultiMeshes or template Meshes from being quarantined as unreachable.
- `gameplay` merges MultiMesh objects across venue sections, preserves exact
  Group references, binds the decoded GH1 `crowd.env` after the merge, and
  leaves GH2 WorldCrowd placement meshes non-draw.
- `milo_scene_renderer` expands source MultiMesh transforms in authored order
  and propagates Group environment state to their template Meshes. It also
  reconstructs the matched binary crowd silhouette only for a fully opaque
  texture used by an alpha-blended MultiMesh template.
- Contract tests reject the removed crowd-name heuristic, synthetic culls,
  fake floor material substitutions, and dangling environment ownership.

## Validation

The input-free runtime matrix covers 15 venues:

- GH1: 35 MultiMesh objects, 2,265 authored instances, 2,265 submitted, zero
  missing template Meshes, and one source environment bind in every venue.
- GH2: 49 WorldCrowd actors and 4,080 decoded placements, with no GH1
  MultiMesh path used; 4,080/4,080 flat billboards are submitted and visible
  under the source front-face mapping.
- GH2 animation: 49/49 actors change sampled-pose digest, with source clip
  path and channel count logged.
- GH1 card animation: zero direct card animation references; 24 decoded
  crowd-associated LightAnim objects remain in the normal venue animation
  path.
- Every run reaches `playing`, exits zero, and reports zero decode errors.
- No run reports a manufactured visible crowd-floor row.
- A fresh input-free Theatre run submits 500/500 MultiMesh instances, logs
  `source_multimesh_silhouette changed=25220`, and shows no rectangular card
  backgrounds. An Arena control run retains 25,220 authored transparent
  pixels and does not activate reconstruction.

The full converter audit covers 105 assets with 105 complete, zero incomplete,
13,115 converted objects, 815 source-derived synthesized objects, zero
blocked objects, and 13,930 emitted semantic objects.

Focused `milo_convert_test` and `ghogx_milo_scene_test` pass. The broad
gameplay contract runner retains 64 unrelated stale baseline failures, while
all new crowd source-contract assertions pass.

The compact evidence set is in
`proofs/gh1-native-conversion-parity/crowd-floor-source-contract/`.
