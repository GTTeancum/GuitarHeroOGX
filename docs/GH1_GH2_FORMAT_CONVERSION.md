# GH1 to GH2 native asset-format conversion

## Purpose

This document is the executable contract for converting retail Guitar Hero 1
PS2 characters and venues into retail Guitar Hero 2 PS2-native assets. Runtime
compatibility is useful evidence, but it is not the output of this work. The
target is an offline converter whose output can be loaded through the ordinary
GH2 object, directory, archive, animation, and renderer paths without a GH1
runtime adapter.

A conversion is not considered understood when it merely looks plausible. A
format layer is closed only when:

1. Every consumed source byte belongs to a named field, declared padding, or a
   documented opaque field proven irrelevant to GH2 behavior.
2. Every object and directory reference is resolved by type and identity.
3. Parse followed by save is byte-exact when the model is unchanged.
4. A changed model saves deterministically and reparses to the same semantics.
5. The GH2 target revision and inheritance order are supported by a writer.
6. Complete packed-asset sweeps have no unclassified revisions, residual bytes,
   dangling references, or decoder fallbacks.

Names, paths, character identities, venues, meshes, bones, materials, poses,
and byte offsets may be test inputs. They may not select parsing rules or
conversion corrections.

## Evidence policy

Evidence strength, from strongest to weakest:

1. Retail packed bytes plus a byte-accounted parser/writer and round-trip test.
2. Retail executable load/save behavior established by read-only PCSX2 memory
   traces or static disassembly.
3. Matching open-source engine/editor implementation for the same class and
   revision, checked against retail bytes.
4. Cross-file invariants demonstrated over the complete packed inventory.
5. Visual/runtime behavior. This validates semantics but cannot establish field
   order or prove that ignored bytes are irrelevant.

Guessed layouts, marker scans, asset-specific offsets, and visual compensation
are discovery aids only. They are not format contracts.

GH2 class layouts are not a discovery gap. The authoritative target
read/write contract is the checked-out
`ihatecompvir-public-milo-sources/MiloEditor` implementation at commit
`3c02a5497ede1a5d61023fb066cc8bfbe2e8a8e4`. Retail GH2 bytes are used to
verify that this converter emits compatible target objects and to preserve
platform-specific cache/packing behavior; they are not used to invent a
second GH2 schema. The unknown side of each mapping is GH1 field meaning and
how that meaning is represented by the established GH2 classes.

The source audit includes
[`PikminGuts92/pikaxe`](https://github.com/PikminGuts92/pikaxe) commit
`d35227d8d8dc03e248bb817f98b273b3e27572f9`. It is useful corroboration for
many later class fields and target revisions, but it is not accepted as a
complete contract: its directory reader explicitly guesses object sizes by
searching for `ADDE` markers, its directory metadata/external dependency work
contains TODO paths, and several animation writers select convenient output
revisions or leave later fields unsupported. The local byte-accounted
readers/writers and packed sweeps close those limitations independently;
Pikaxe is corroboration, not a dependency of the converter.

Animation-transition semantics are additionally corroborated against
[`DarkRTA/rb3`](https://github.com/DarkRTA/rb3) commit
`ababaa9bb6a669af06aa7def7dd735c4f5265061`. Its `CharGraphNode` declaration
names the two floats as the beat where blending leaves the current clip and the
beat where it enters the next clip. Its `CharClip::Transitions` implementation
groups those nodes by target clip. This later engine source supplies field
meaning; the GH1 packed files independently establish the ACG byte layout.

## Packed source inventory baseline

The audited GH1 retail `MAIN.HDR` is ARK v3 with one ARK part and 3,167
entries. Relevant extensions include 105 `rnd_ps2` object directories, 926
standalone `acp` clips, 25 `acg` graphs, 2 `acs` sets, 156 `dtb` trees, 61
`gh` resources, 16 song `seq` trees, and 1,523 PS2 bitmap resources
(`bmp_ps2` plus `png_ps2`).

A clean GH2 USA retail image is established independently by Redump MD5
`317968cd573b183f3c697bb94e8c8ee6` (`SLUS-21447`, version 1.00). Its extracted
base files are:

- `MAIN.HDR`: SHA-256
  `027176E241DE0BC28AF347403E9732E47260ED14A0FB322BB65FCC20D23FD4FD`;
- `MAIN_0.ARK`: 3,118,656,871 bytes, SHA-256
  `8CADFB195D6783A7A95A72F3A4DBB56695A364452BBE96881F4166A437520D21`.

Those hashes identify the immutable target corpus used to recover and test the
GH2 contracts. The deployed hybrid now contains a manifest-driven overlay:
`main.hdr.pre-overlay.bak` retains the original 78,602-byte index, and the
original ARK byte range remains unchanged while converted entries are appended
and the active header is rebuilt. The clean base contains 488 revision-24
`milo_ps2` directories and 179 DTBs.

Sample files extracted during audits live under ignored build directories.
Copyrighted packed assets and decompressed bodies must never be committed.

`tools/format_audit` now performs the archive sweep in memory and emits a TSV
ledger plus a type/revision ledger. The first complete sweep proves:

- all 105 GH1 revision-10 object directories retain byte-exact outer
  containers and structural directory prefixes, and have one unique complete
  type/revision-constrained object-terminator chain to payload EOF;
- all 156 GH1 DTBs retain byte-exact storage;
- all 16 zero-prefixed song SEQ trees retain byte-exact storage;
- all 926 GH1 ACP files parse and save byte-exactly with no trailing bytes;
- all 25 GH1 ACG files parse and save byte-exactly with no trailing bytes;
- both GH1 ACS files parse and save byte-exactly, and both compiled include
  references resolve to packed `gen/*.dtb` entries;
- all 926 ACP bodies are revision 18, use compression mode 1 in both channel
  sets, and carry SampleSet serialization revision 5 at body offset 28;
- expanding the two manifests produces 31 clip-set records and 926 animation
  records; 22 manifest-owned graphs cover 691 clips. The other three ACGs are
  byte-exact 24-byte, one-clip/one-node editor residuals with no ACS owner and
  no exact string reference in any packed DTB or SEQ. The audit classifies
  these structurally and rejects any unowned graph that is not an
  unreferenced single-node residual;
- all 926 ACPs use set 0 only for multi-sample channels and set 1 only for
  one-sample constants, with zero cross-set channel overlap and no facing
  channel in set 1;
- all 488 clean GH2 revision-24 MILOs retain byte-exact outer containers
  and structural directory prefixes;
- all 179 clean GH2 DTBs retain byte-exact storage;
- all 1,214 audited GH1 files in the five format families (`dtb`, `rnd_ps2`,
  `acp`, `acg`, and `acs`) pass their current outer-format contracts;
- all 12,189 object bodies in the 105 GH1 revision-10 directories parse and
  reserialize byte-exactly through semantic, revision-aware readers and
  writers: 21 of 21 observed type/revision rows, zero failures, zero residual
  bytes, and no asset-name or offset exceptions.

The full sweep corrected a sample-based assumption: three Grim wing ACPs have
an empty channel set with a nonzero sample count. Because its frame width is
zero, the set consumes no sample bytes but retains the shared timeline count.

### GH1 object type/revision inventory

The revision-10 directories contain exactly these 21 observed body revisions:

| Type | Revision | Count |
|---|---:|---:|
| Cam | 9 | 20 |
| CamAnim | 0 | 6 |
| EnvAnim | 3 | 82 |
| Environ | 1 | 161 |
| Flare | 3 | 35 |
| Font | 7 | 20 |
| Light | 3 | 127 |
| LightAnim | 1 | 88 |
| Mat | 21 | 1,693 |
| MatAnim | 5 | 156 |
| Mesh | 25 | 7,087 |
| MeshAnim | 0 | 18 |
| Morph | 3 | 14 |
| Movie | 6 | 2 |
| MultiMesh | 0 | 41 |
| ParticleSys | 22 | 76 |
| ParticleSysAnim | 2 | 60 |
| Tex | 8 | 1,067 |
| Text | 15 | 72 |
| TransAnim | 4 | 686 |
| View | 7 | 678 |

The corresponding current revision-24 target inventory uses, among others,
Cam 12, CamAnim 2, EnvAnim 4, Environ 5, Group 12, Light 6, LightAnim 2,
Mat 27, MatAnim 7, Mesh 28, MeshAnim 1, ParticleSys 27,
ParticleSysAnim 3, Tex 10, Trans 9, TransAnim 6, Spotlight 20, and
WorldCrowd 6. Character targets additionally use Character 9,
BandCharacter 1, CharBone 2, and CharClipSamples 10. These are observed target
revisions. The complete conversion sweep, target reparse, reference audit, and
native runtime sweep described below prove the selected mapping for every
observed GH1 source object.

## Layer status

| Layer | Read contract | Write contract | Current evidence | Status |
|---|---|---|---|---|
| ARK v3 HDR/ARK | Lossless header, string tables, entries, split-ARK offsets | Deterministic header/index builder and append-only manifest overlay | Byte-exact GH1/clean-GH2 headers; three overlays are idempotent on the second pass | Closed for the deployed one-part PS2 contract |
| MILO outer container | NONE/GZIP/A/B/C/D read; exact block retention | Deterministic changed A/B blocks; unchanged structures retain source bytes | All emitted character, animation, and venue MILOs reparse; repeated conversion hashes match | Closed for observed GH1/GH2 PS2 storage |
| MILO object directory | Revision-aware prefix, type/name table, allocation hints, revision 7-16 external resources; exact GH1 and GH2 body boundaries | Complete deterministic revision-10 and revision-24 directory rebuild | All 105 GH1 directories rebuild byte-exactly; generated GH2 roots and children reparse exactly | GH1 rev10 and GH2 rev24 write framing closed |
| MILO object bodies | Semantic readers for all 21 observed GH1 type/revision rows; GH2 target layouts sourced from ihatecompvir | Lossless semantic GH1 writers and target serializers for every required mapping | 12,189/12,189 GH1 bodies exact; 13,115 converted plus 696 synthesized target objects, zero blocked | Closed for the complete packed conversion inventory |
| Object/reference graph | Schema-typed null, internal, external, parent, target, owner, material, texture, bone, group, and controller references | Deterministic name/fixup emission | 16,637 character references and 14,581 venue references accounted; zero dangling | Closed |
| Classic DTB | All currently recognized classic node tags, storage form, seed, line metadata, trailing bytes | Plain, zero-prefixed, and encrypted serialization | Byte-exact real GH1/GH2 samples | Closed for recognized tags |
| GH1 ACP | Standalone wrapper, revision-18 body, and revision-5 SampleSet decoding | Lossless deterministic serialization | All 926 packed ACPs byte-exact plus GH1 load/save code | Closed for observed revisions |
| GH1 ACG | Version-1 clip-indexed transition graph | Lossless deterministic serialization | All 25 packed ACGs byte-exact plus later engine declarations | Closed for observed version 1 |
| GH1 ACS | Lossless line grammar for includes, comments, blanks, and macro invocations | Lossless deterministic serialization | Both packed manifests byte-exact; 2/2 includes resolve | Closed for observed manifests |
| PS2 HMX bitmap | Header/payload read and RGBA decode for observed PS2 encodings | Tex8-to-Tex10 retains the byte-identical same-platform bitmap payload and rewrites only the target object wrapper | 1,067 source Tex8 bodies; 2,566 clean-target Tex10 round trips; full native venue/character texture loads | Closed for observed PS2 encodings |
| GH2-native converter | Semantic models for all observed GH1 body rows and required GH2 target rows | Systemic model, animation, controller, face, config, song-MIDI, and venue package emission | 105/105 directories, 926 clips, 13 character packages, 7 venues, zero blocked; native runtime loads use `char/...` and `world/gh1_...` | Structurally closed; matched runtime parity remains open |

## Proven MILO outer-container contract

Retail GH1 and GH2 PS2 character and venue containers use `MILO_B`
(`0xCBBEDEAF`) raw-DEFLATE blocks. Audited retail files begin block data at
`0x210`: a 16-byte header followed by storage for 128 32-bit block-size slots.
Only `block_count` slots are live. The remaining bytes are not reliably zero,
so they are retained verbatim for unchanged files and zeroed only in a newly
constructed deterministic container.

`gh::milo::Container` retains:

- the complete pre-block prefix;
- each original size-table value;
- the original compressed bytes;
- the corresponding uncompressed bytes;
- trailing bytes after the declared blocks.

Unchanged blocks reuse their original compressed representation. Changed
`MILO_A` and `MILO_B` blocks are encoded deterministically. Current real-file
proofs are byte-exact for:

- GH1 `charsys/alterna/gen/alterna.rnd_ps2`;
- GH1 `venues/theatre/gen/theatre.rnd_ps2`;
- GH2 `char/alterna2/og/gen/alterna2.milo_ps2`;
- GH2 `world/theatre/gen/theatre_bank.milo_ps2`.

This closes only the outer block container. It does not validate object
directory boundaries or any object body.

## Proven MILO directory-prefix contract

The directory prefix is now modeled independently from child bodies:

- the directory revision is always first;
- revision 14+ carries the root type, root name, and two allocation hints;
- the object table is a signed count followed by length-prefixed type/name
  pairs;
- revisions 7 through 16 carry a post-table external-resource count and
  length-prefixed paths;
- revision 17+ begins the root directory object's own serialized body
  immediately after the object table.

The parser retains every field above and the writer deterministically
reproduces the prefix. Full sweeps are byte-exact for every GH1 revision-10 and
clean GH2 revision-24 directory prefix.

GH1 child framing no longer uses a nearest-marker or Mesh-specific heuristic.
For every declared table entry, the solver requires the body to begin with the
retail revision assigned to that declared type, requires its terminator to
lead to the exact revision of the next declared type, requires the last
terminator to end at payload EOF, and rejects zero or multiple complete
solutions. All 105 packed directories have exactly one solution across all
21 types and 12,189 objects. This establishes the observed GH1 body slices
without confusing marker-shaped texture/sample bytes for terminators.

Each proven slice and terminator is retained independently. The complete
revision-10 writer rebuilds the table, external-resource vector, raw class
bodies, and separators; all 105 decompressed payloads reproduce byte-exactly.
Synthetic edit coverage renames a table object and changes its body, reparses
the result, and reproduces the edited payload. Every observed GH1 child body
also has a semantic reader and writer; the structural layer remains independent
so directory framing is not inferred from class contents.

Directory revision 10 is not treated as proof of GH1 provenance by itself.
Generated/custom UI directories can use the same revision with later classes
such as `LabelEx`. Unknown rows use a packed-revision structural fallback for
read-only consumers and are explicitly marked non-exact/non-writable; only the
complete known 21-row retail contract may enter the revision-10 writer.

Retail `DirLoader` still calls each class loader before checking the
`AD DE AD DE` separator. The unique chain first proved structural framing
independently; the complete semantic sweep below now proves exact consumption
inside every GH1 slice as a second check. The GH2 root and child class order is
defined by ihatecompvir. Local revision-24 deterministic serialization is now
implemented and reparse-proven for native character clip-set packages; G3
retains only recursive package/reference closure outside that completed path.

## GH1 object-body consumption ledger

The format audit feeds each proven GH1 slice to a source-order class reader and
requires cursor equality with the body end, then serializes the typed value and
requires byte equality with the original slice. The packed-archive result is:

| Type/revision | Exact bodies | Residual/fail | Proven GH1 body contract |
|---|---:|---:|---|
| Cam 9 | 20 | 0 | Trans8, Drawable1, near/far/FOV, screen rectangle, Z range, target texture |
| CamAnim 0 | 6 | 0 | Animatable0, camera ref, FOV keys, keys owner |
| EnvAnim 3 | 82 | 0 | Animatable0, environment ref, ambient/fog colors and fog-range keys |
| Environ 1 | 161 | 0 | Obsolete Drawable block, drawable refs, light refs, ambient/fog state |
| Flare 3 | 35 | 0 | Trans8, Drawable1, material, two-axis size, range, steps |
| Font 7 | 20 | 0 | Material, cell metrics, character string, optional kerning table |
| Light 3 | 127 | 0 | Trans8, RGBA color, range, serialized light type |
| LightAnim 1 | 88 | 0 | Animatable0, light ref, color keys, keys owner |
| Mat 21 | 1,693 | 0 | Texture stages, transforms/wrap, blends, color, compact render state |
| MatAnim 5 | 156 | 0 | Animatable0, material ref, stage transform/texture keys, color/alpha keys |
| Mesh 25 | 7,087 | 0 | Trans8, Drawable1, geometry refs/state, vertices, faces, patches, bones, PS2 strip caches |
| MeshAnim 0 | 18 | 0 | Animatable0, mesh ref, point/UV/color vector keys, keys owner |
| Morph 3 | 14 | 0 | Animatable0, pose Mesh refs and keys, target, flags/intensity |
| Movie 6 | 2 | 0 | Animatable0, file and texture refs, stream/loop state |
| MultiMesh 0 | 41 | 0 | Drawable block, source Mesh ref, instance transforms |
| ParticleSys 22 | 76 | 0 | Animatable0, Trans8, Drawable1, emission/state, emitter Mesh, preserved rows |
| ParticleSysAnim 2 | 60 | 0 | Animatable0, particle ref and color/rate/speed/life/size keys |
| Tex 8 | 1,067 | 0 | Dimensions/type/external state plus optional 32-byte HMX bitmap header and payload |
| Text 15 | 72 | 0 | Drawable, Trans8, font/text/layout/color/markup state |
| TransAnim 4 | 686 | 0 | Animatable0, Drawable, target and rotation/translation/scale key tracks |
| View 7 | 678 | 0 | Animatable0, Trans8, Drawable1, children owner, showing range |
| **Total** | **12,189** | **0** | **21/21 observed type/revision rows** |

The Environ sweep corrected an earlier renderer assumption: its obsolete
drawable list is typed to `RndDrawable`, not Mesh. Retail Big Club data
legitimately references `light_big.view`; directory fixup must enforce the
target class instead of a filename-suffix allowlist.

Morph revision 3 also corrects an incomplete third-party layout: every pose
starts with its Mesh reference, each key stores value then frame, and the pose
vector is followed by the target Mesh before normals, spline, and intensity.
The later engine `RndMorph::Pose`/`RndMorph::Load` declarations corroborate
that ownership and order; all 14 GH1 bodies independently prove it and
round-trip exactly.

The higher-risk tail contracts are also classified rather than retained as
opaque residuals:

- Tex8 embeds a 32-byte HMX bitmap header followed by its exact platform
  payload when the body is not external-only.
- View7 is exactly Animatable0, Trans8, Drawable1, children owner, and showing
  range. The former apparent four-byte flag prefix was the Drawable revision
  word; no transform-marker scan is needed. Its Drawable list is ordered state,
  not merely a set: an Environ entry changes the environment for the drawables
  which follow it. Conversion therefore segments the stream at each Environ,
  emits deterministic native `<view>.__environment_<index>.grp` children, and
  retains their order through `<view>.__draw_only.grp`. A multi-scope View is
  rejected by the flat View-to-Group helper instead of silently selecting the
  first Environ for every child.
- ParticleSys22 stores `bounceEnabled` plus a four-float plane after its four
  endpoint colors, then force direction and material. It stores emitter Mesh
  after relative motion, and each preserved particle is a 32-byte
  position/color/size row.
- Mesh25 stores 48-byte vertices (position, normal, a shared four-float
  color/weight slot, UV), 16-bit triangle indices, byte patch sizes, up
  to four fixed bone slots with 3x4 matrices, and one last-generation PS2 strip
  cache per populated patch. No packed GH1 Mesh has a non-null BSP tree.

This closes the byte layout and lossless GH1 read/write contract for every
observed class revision. Mat21's complete field semantics are also closed by
the retail GH1 `RndMat::Load` at `SLUS_212.24:0x001BE900` and its debug routine
at `0x001BE458`: each texture-stage pair is stage blend/TexGen, and the tail is
`useEnviron`, `vertexAmbient`, `vertexDynamic`, `cull`, `multipass`,
`normalize`, `zMode`, `alphaCutout`, and `alphaWrite`. The Mat21-to-Mat27
mapping is closed by the source-backed revision-21 upgrade behavior: the first stage
becomes the root Mat27, every later stage becomes a deterministic
`<root>_<index>.mat`, and `nextPass` links the chain. Stage blend becomes the
pass blend, non-root passes use transparent Z mode, `MultipassSrc` resets
non-root passes to unlit white, and the obsolete `normalize` field is consumed
without a target serialization. Focused tests cover a two-stage multipass
material and its animated-stage routing; the packed reference audit covers the
generated pass links.

## Semantic source-to-target field ledger

The converter now carries a class/revision-keyed field contract rather than
using a successful target reparse as a proxy for semantic completeness.
`semantic_field_contract.cpp` defines two independent sets:

1. the serialized semantic fields implied by each revision-aware source
   reader; and
2. the target mapping for each field, classified as retained, translated,
   synthesized, or intentionally discarded.

The unit test requires exact field-set equality for every supported source
class/revision. The packed audit repeats that gate for every encountered
object before conversion and emits
`conversion-audit.tsv.fields.tsv`. The 2026-07-27 result covers all 21
observed MILO classes, standalone ACP revision 18/sample-set revision 5,
Classic DTB/SEQ container and node fields, all 201 VenueCam records, and all
seven venue scripts: 762 rows, including 603 source fields and 159 target-only
synthesized fields, with no missing schema fields. The added target row records
the deterministic environment-scope Groups synthesized from ordered View7
Environ state.
The DTB/SEQ leg byte-round-trips all 172 packed trees (156 encrypted DTBs and
16 zero-prefixed SEQs), 87,977 nodes, 13 observed container control words, and
zero trailing bytes. Song SEQ control words such as 779 are preserved metadata,
not class-layout revisions. Venue scripts account for every top-level root,
including 10 validated `load_section` links and Fest's one state initializer.
The same run remains 105/105 MILO directories, 926/926 ACP files, 13,115
converted objects, 793 synthesized objects, and zero blockers.

The discard-value report prevents nonzero fields from being hidden behind that
classification. It found 157 Mat21 bodies with `vertexDynamic=true` and 14
with `normalize=true`. Static tracing of the actual GH2 retail
`SLUS_214.47` revision-21 loader proves that the third legacy lighting boolean
is read at `0x001C002C` without a target store, and `normalize` is read at
`0x001C02E4` without a target store. Mat27 has no corresponding serialized
members. Authored `cull` is different: although GH2's compatibility path also
loses it, native Mat27 can represent it, so the offline converter preserves
the source value. This is required for two-sided GH1 props such as Small
Club's records.

The discard report now has a source-addressable companion rather than only
aggregate buckets. The full audit emits 17,314 instance rows across 1,030
archive assets and 11,471 objects, each with archive path, object name,
class/revision, field, value class, and proof status. All 28 aggregate buckets
reconcile exactly. The result contains 15,415 default/absent instances, 867
structural revisions, 861 legacy drawable fields whose native target classes
are not drawable, 171 retail-traced Mat21 discards, and zero unresolved
nondefault values. The six `Cam9.drawable.showing=false` rows are all the
authored `6 foot camera.cam` helper in six venues; this is legacy drawable
state, not camera selection.

The ledger is a schema and conversion-rule proof. The intentional-discard
instance subcase is closed. Mat21 is now closed at the retained/translated
value level as well: two machine reports record all 1,693 source roots and
1,181 texture stages beside their reparsed Mat27 values. An independent
formula audit verifies global blend/color/environment/prelit/cull/Z/alpha,
stage blend/TexGen/wrap/transform/texture, pass count/linkage/defaults,
disabled root textures, SrcAlpha intensify, and MultipassSrc resets with zero
root or stage mismatches. Mesh25's non-transform fields are likewise closed:
the report pairs all 7,087 source meshes with reparsed Mesh28 targets and finds
zero mismatches across 356,288 vertices, 411,158 faces, 1,609 skinned meshes,
drawable/material/owner/mutable/volume state, patch/group layout, BSP
sentinels, bone slots/matrices, and cached strip sections. Tex8 is closed by
another per-instance report covering all 1,067 textures and all 16,535,680
embedded bitmap payload bytes with zero wrapper, header, reserved-byte, or
payload mismatches. ACP18 is closed by a 926-row report covering all 42,386
channel entries and 18,810,192 compressed sample bytes. TransAnim4 retained
payload/filter reduction is closed across 686 animations, 30,263 keys, and
163 synthesized filters. View7 membership is closed across all 678 Views,
1,132 animation references, 5,433 drawable references, and all synthesized
scope/draw-only Groups. Seven further direct animation families are closed
across 270 objects and 4,502 keys. MatAnim5 is closed across 156 objects and
183 classified stage outcomes. Six simple object families are closed across
456 instances and 6,588 field rows, and ParticleSys22 is closed across 76
instances and 3,737 field rows. Font7 is closed across 20 instances and 5,540
field rows. The directory
transform graph is closed by a separate 8,095-row report covering all seven
source transformable classes and all 4,545 child links. Other class families
and matched retail visual gates remain open. The machine-readable reports and
scope statement are under
`proofs/gh1-native-conversion-parity/semantic-field-contract/`.

## Closed Mesh25-to-Mesh28 mapping

The first target conversion is source-defined and systemic:

- GH1 Mesh25 `Trans8` becomes GH2 `RndTransformable` revision 9. Local/world
  matrices, constraint, target, preserve-scale, and parent are retained.
  Trans8's obsolete serialized child vector is not a GH2 field; parent/owner
  fixups reconstruct the graph at directory-conversion time.
- GH1 `Drawable1` becomes GH2 `RndDrawable` revision 3. Showing state and the
  bounding sphere are retained; the obsolete drawable vector is represented
  by converted Group membership. GH2 draw order starts at the class default.
- Material, geometry owner, mutable flags, volume, the 48-byte PS2 vertex rows,
  16-bit faces, byte group sizes, four fixed legacy bone slots, 3x4 bone
  matrices, and cached PS2 strip rows transfer without reinterpretation.
- Empty GH1 BSP becomes the GH2 null BSP root. All 7,087 packed GH1 meshes have
  a null BSP; no asset-specific BSP rule exists.
- Cached strip sections remain optional. Transform-only/geometry-owner proxies
  can retain nonzero copied group sizes while omitting a local cache payload.

The corpus-wide value report makes this mapping instance-addressable:
`conversion-audit.tsv.mesh-values.tsv` records source and target values,
counts, and stable digests for every field above. It covers all 7,087 meshes,
356,288 vertices, 411,158 faces, and 1,609 skinned meshes with zero
non-transform mismatches. It also records the 1,139 nonempty legacy child
vectors as `directory_transform_graph`; those vectors drive the later
directory-level parent/transform fixup and are not a native Mesh28 field.

`parse_mesh28`/`serialize_mesh28` are checked against all 11,365 Mesh28 bodies
in the clean GH2 archive: 11,365 exact, zero failures, zero residual bytes.
The boundary proof validates the complete Mesh28 body before accepting an
object terminator, including BSP nodes, bones, and strip caches, so marker
bytes inside cached data cannot truncate a mesh. The complete GH1 regression
remains 7,087/7,087 exact. Synthetic conversion coverage reparses and
round-trips a changed Mesh25-to-Mesh28 model.

## Closed revision-8 directory transform graph

GH1 serializes both an ordered child list and an explicit parent on each
revision-8 transformable. GH2's compatibility load is order-sensitive:

1. when a source object is processed, each resolved child is assigned that
   object as its parent;
2. the object's explicit parent is then processed;
3. a self parent is a sentinel that retains the current child-list parent;
4. every other explicit parent, including null, overrides the current parent
   and forces old-parent constraint value 2; and
5. a child assignment from a later source object can subsequently replace
   that parent while retaining the constraint state.

The offline converter reconstructs that state directory-wide before emitting
native revision-9 transformables. The independent
`conversion-audit.tsv.transform-values.tsv` proof covers all 8,095 instances:
7,087 Mesh, 678 View-to-Group, 127 Light, 76 ParticleSys, 72 Text, 35 Flare,
and 20 Cam objects. It replays all 4,545 child links in source order and
compares reparsed target local/world matrices, parent, constraint, target, and
preserve-scale values. There are zero unresolved links and zero mismatches.
The corpus contains 4,507 self-parent objects with a child owner, 1,928
self-parent objects without one, 1,653 explicit-parent wins, seven later-child
wins, and 21 objects with multiple child-owner assignments.

## Closed Tex8-to-Tex10 value mapping

Tex10 adds a native ObjectFields0 wrapper but otherwise retains the GH1 Tex8
state for this same-platform PS2 conversion. Width, height, nominal BPP,
external path, mipmap bias, texture type, use-external flag, bitmap presence,
the complete 32-byte HMX bitmap header, its 17 reserved bytes, and the raw
bitmap payload transfer without reinterpretation.

`conversion-audit.tsv.tex-values.tsv` records all 1,067 source/target pairs.
Every source contains an embedded bitmap, totaling 16,535,680 payload bytes.
An independent check reports zero mismatches and verifies the synthesized
ObjectFields0 wrapper remains at native defaults. The observed corpus contains
1,065 encoding-3 and two encoding-1 bitmaps; BPP values are 4, 8, 24, and 32,
and 48 textures retain an authored use-external flag without losing their
embedded fallback bitmap.

## Closed ACP18-to-CharClipSamples10 value mapping

GH1 standalone ACP revision 18/sample-set revision 5 stores clip timing,
flags, two ordered channel sets, per-set compression/sample counts, and the
compressed sample bytes. GH2 CharClipSamples10 retains the timing and sample
streams but translates three structural details:

1. the GH1 bit-31 export marker is removed from gameplay flags;
2. the old time/alignment enum maps into the GH2 play-time bit domain; and
3. each ordered channel set gains the ten cumulative CharBones category
   boundaries derived from `.pos`, `.scale`, `.quat`, Euler, and delta-Euler
   suffixes.

`conversion-audit.tsv.acp-values.tsv` pairs all 926 ACPs with reparsed native
targets. It verifies 42,386 channel entries, recomputed frame sizes, cumulative
category counts, and 18,810,192 compressed sample bytes with zero mismatches.
All 926 sources carry the removed export marker. Observed source play flags
are 0, 2, 4, and 16, and every instance maps to the corresponding native
value. The synthesized ObjectFields/range/event state stays at defaults. The
third legacy CharBones header retains the full-set compression/sample-count
metadata but, matching the GH2 serialized contract, contains no channels,
counts, or sample-data block.

## Closed TransAnim4-to-TransAnim6 payload mapping

TransAnim6 adds native ObjectFields0/Animatable4 bases and removes the legacy
Drawable base. Its authored payload is otherwise retained: target reference,
quaternion rotation keys, translation keys, keys owner, translation
spline/repeat flags, scale keys, scale spline, follow-path, and rotation-slerp.

`conversion-audit.tsv.trans-anim-values.tsv` pairs all 686 source animations
with reparsed targets and verifies all 30,263 ordered key rows and every
payload flag with zero mismatches. A reducer independent from the converter
also checks all 312 legacy Animatable0 operations and all 163 required
AnimFilter1 targets, including absolute scale, offset, range, loop/type, target
animation, and constructor defaults. The corpus has three legacy animation
membership references and no TransAnim drawable memberships; the three
memberships are carried by the separately audited View-to-Group graph.

## Closed View7-to-Group membership mapping

A View serializes its own ordered animation and drawable member lists.
`children_owner` is a separate legacy ownership/editor relationship; it is not
an alias for another View's lists. This distinction is observable in retail
Theatre: `verse.anim` names `chorus.anim` as its `children_owner` while its own
animation members are the `verselight01/02/03.envanim` tracks. Following the
owner silently assigned chorus lighting to the verse state. The converter and
independent audit now both traverse the lists on the source View itself.

The converter resolves those lists as graphs: nested Views retain Group
boundaries, non-View drawable membership is expanded recursively with cycle
detection, MatAnim references gain every non-static split pass, and non-View
Animatable0 membership is recursively expanded with the same cycle gate. A
focused regression fixture gives `verse.anim` and `chorus.anim` distinct member
lists while sharing the legacy owner link and requires the converted verse
Group to contain only its own members.

The fresh full-corpus audit in
`proofs/manage-band-20260906/gh1-view-member-regeneration-v4/` covers all 678
Views and reports all 678 exact. It independently reconstructs 1,161 animation
references and 5,402 drawable references, then compares every reparsed primary
Group list. Ordered Environ changes require 70 native scope Groups; 26 Views
require a native draw-only Group, and 10 require an AnimFilter. The same rebuild
converted 105/105 assets with zero blockers, all seven venue camera tables (201
records and 191,626 emitted keyframes), all seven venue scripts, all 33 venue
assets, and all 28 placement audit rows.

The recursive animation closure fixed a corpus-wide omission rather than
special-casing assets. The two source instances are:

- Arena `arena_fire_flash.tnm` owns `arena_fire_flash.mnm`.
- Track `gem_bonus_spark1.tnm` owns `gem_bonus_spark2.mnm` and
  `gem_bonus_spark1.mnm`.

Their enclosing Groups now retain those ordered MatAnim members.

## Closed direct animation payload families

`conversion-audit.tsv.animation-payload-values.tsv` covers all 270 Morph,
LightAnim, CamAnim, EnvAnim, ParticleSysAnim, MeshAnim, and Movie objects. It
compares 4,502 authored key rows, retained references and owners, Morph
poses/flags/intensity, Movie stream/loop flags, native base defaults, and all
19 required filters. CamAnim's FOV values are independently recalculated with
the retail horizontal-to-vertical conversion before comparison. All normalized
payload digests match, and these families contain no additional nested
Animatable0 memberships.

## Closed MatAnim5-to-MatAnim7 stage mapping

MatAnim5's final serialized stage remains on the original object. Each earlier
stage with a nonzero end frame becomes a one-based `<anim>_N.mnm` pass
targeting `<material>_N.mat`; its keys owner becomes the generated animation
name. Earlier frame-zero stages are consumed by the retail loader rule rather
than emitted. A source-authored MatAnim already carrying a generated split
name supersedes that generated object.

`conversion-audit.tsv.mat-anim-values.tsv` accounts for all 156 source
objects, 183 stage outcomes, 994 keys, and 89 filters. All 156 root passes and
13 emitted split passes compare exactly. Twelve early frame-zero stages are
marked retail-consumed. The two remaining outcomes are authored
`amp_inside_star_1.mnm` overrides in the single-player and multiplayer HUD
packages; their values intentionally supersede the generated stage. The rule
is name-collision behavior decoded from the loader, not an asset-specific
branch.

## Closed simple-object payload mappings

`conversion-audit.tsv.object-field-values.tsv` closes every packed Cam9,
Environ1, Flare3, Light3, MultiMesh0, and Text15 instance. The audit reparses
each emitted GH2 body and independently compares 6,588 fields across 456
objects. It covers retained payloads, Drawable conversion, target-native
ObjectFields0 and newly introduced field defaults, reference arrays,
MultiMesh transform arrays, and the Cam9 horizontal-FOV to Cam12 vertical-FOV
formula `2 * atan(0.75 * tan(source_fov / 2))`.

All rows are exact. Removed legacy Drawable object-list and target fields are
empty in every affected source instance. The result therefore requires no
asset-name exception and leaves no unaccounted value in these six classes.

For `MultiMesh0`, field-exact conversion is now joined to source-exact runtime
ownership. All 41 source objects are crowd archetypes found in eight venue
packages. The converter emits an ordered
`__gh1_runtime_multimeshes.grp` corresponding to retail `Arena::Crowd`, while
the renderer expands every serialized transform through the referenced
template Mesh without drawing that template separately. Cross-directory
`crowd.env` cannot be represented by a standalone package ObjectPtr, so it is
resolved and assigned to the owner Group only after all venue sections merge.
This matches the retail `Arena + 0x9C` environment field and leaves no dangling
serialized reference.

The accompanying texture audit proves that conversion must not normalize
crowd alpha. Seven packages share a binary-alpha bitmap with 25,220 transparent
and 7,548 opaque pixels. Theatre's source bitmap carries the same RGB but is
fully opaque. Both source forms remain byte-identical to their respective raw
payload after conversion. Runtime treats all indexed storage as CLUT-alpha;
only a fully opaque image used by an alpha-blended `MultiMesh0` template
receives the exact binary silhouette established by the seven matching source
packages. This fixes Theatre without a venue/name rule or a global black key.
Runtime coverage submits all 2,265 GH1 instances with zero missing templates;
the native GH2 path separately retains 49 `WorldCrowd6` actors and 4,080
placements. Details and proof are in
`docs/CROWD_AND_FLOOR_SOURCE_PARITY.md`.

Report SHA-256:
`443CD2E341975FD3AC919E8F22068D9D794043DE4C6029464C29FA97BF9D8B9F`.

## Closed ParticleSys22-to-ParticleSys27 mapping

`conversion-audit.tsv.particle-values.tsv` reparses and independently compares
all 76 packed systems across 3,737 field rows. The ledger covers every
retained emission/render field, 896 preserved particle rows, Drawable and
native base/default conversion, and the two filters required by legacy
Animatable0 operations. All source Animatable0 membership lists are empty.

The audit independently reconstructs the removed plane equation as a native
Trans9. Thirteen finite nonzero planes match their synthesized local/world
matrices and references exactly. Three enabled zero-normal planes cannot
define collision geometry and remain unbound, matching the loader rule.
Every field row is exact. Report SHA-256:
`232A7278ADB4D29E2B35EC55AE65BDFA9494085D6707DDF967AE4138636A36BB`.

## Closed Font7-to-Font15 mapping

`conversion-audit.tsv.font-values.tsv` closes all 20 packed fonts across
5,540 field rows. Font materials and diffuse textures resolve through the
separately proven Mat21/Tex8 mappings. The audit decodes each source bitmap
and independently regenerates all 5,120 fixed character-info rows from alpha
column bounds, including blank-glyph defaults and the tab-from-space triple
advance rule.

All retained values, 128 kerning rows, 1,896 rasterized character placements,
texture ownership and dimensions, cell-size scaling, native defaults, and
character-table values compare exactly. The corpus does not exercise the
leading-NBSP-to-space compatibility branch, which remains implemented from
the revision loader contract. Report SHA-256:
`54364E4748F479C0A8801A4A8CBC7DECF5A499E569A3E941FF5505A39E5ED84F`.

## Proven ARK v3 header contract

`gh::ark::Index` retains the version, flag, declared part sizes, raw string
blob, string offsets and ordering, five-word file entries, and trailing bytes.
No-edit serialization is byte-exact for the 149,438-byte GH1 retail header and
the 78,602-byte clean GH2 header. The parser bounds every count and
NUL-terminated string against the header, validates name/folder indices, and
checks every entry range against the concatenated declared part sizes.

`make_index` deterministically builds a v3 header for an already planned ARK
layout by normalizing separators and sorting/deduplicating strings.
`ark_tool overlay` consumes the same TSV manifests as loose-file deployment,
reuses byte-identical entries, appends changed payloads without disturbing the
retail prefix, and writes the header only after the planned layout is valid.
The current format-closure character and venue manifests contain 54 and 55
rows. Their verification pass reports all 54 and 55 entries reused with zero
replacements, additions, or appended bytes. The separately completed
singer-face MIDI sweep retains its own 58-row manifest; it is not counted as a
MILO/DTB/ACP converter output. The one-part PS2 archive integration contract
is therefore deterministic and idempotent; split-part reading remains
supported but no split-part writer is required by this target image.

## Proven classic DTB contract

The DTB tree retains the raw version, storage form, cipher seed, node tags,
array line values, float bit patterns, and trailing decrypted bytes.
Serialization supports:

- direct plaintext beginning with `0x01`;
- four-zero-byte-prefixed plaintext used by venue sequence data;
- PS2 stream-cipher storage with its original seed.

Synthetic coverage exercises integer, float, string, variable, symbol, array,
script, and encrypted forms. Real byte-exact proofs cover GH1 camera/venue DTBs
and GH2 character/arena DTBs. Any newly encountered tag is still a hard parse
failure and must be classified before the DTB layer is declared complete for
the full archives.

## GH1 ACP contract

Standalone ACP files begin with length-prefixed class and object names. All
currently audited character clips use class `AnimClipSamples` and body revision
18. The revision-18 body has a 32-byte fixed prefix followed by two channel-set
headers and then the two sample blocks. Each channel-set header contains:

1. channel count;
2. length-prefixed typed channel names;
3. sample count;
4. compression mode.

Declared sample bytes exactly consume the remaining file in audited clips.
Position/scale, quaternion, and scalar-axis channels use type-dependent widths
selected by the compression mode. Empty channel sets still serialize sample
count and compression fields. Three retail Grim wing clips prove that an empty
set may retain a nonzero timeline sample count while consuming zero sample
bytes.

The fixed-prefix words are body revision, start beat, end beat, beats per
second, flags, play flags, blend width, and SampleSet serialization revision.
Static analysis of the GH1 retail executable closes the last field:

- `AnimClipSamples::Load` at `0x0017cdb8` first invokes its base loader, reads
  one 32-bit value into the revision gate at `0x00363b94`, then loads both
  SampleSets through the same revision-aware helper;
- that helper at `0x0017c210` branches on the gate when decoding the SampleSet
  header;
- `AnimClipSamples::Save` at `0x0017ced0` first invokes its base saver, writes
  the constant `5` as a 32-bit value, then saves both SampleSet headers and
  both sample blocks.

The tool consequently names this field `sample_set_revision` and rejects
unproven values rather than applying the revision-5 layout to another format.
All 926 packed ACPs use the proven value and round-trip byte-exactly.

## GH1 ACG transition-graph contract

Every packed GH1 ACG is version 1 and has this complete little-endian layout:

1. `u32 version`;
2. `u32 source_clip_count`;
3. for each source clip, in ACS clip order:
   1. `u32 transition_node_count`;
   2. repeated nodes of `u32 target_clip_index`, `f32 current_beat`,
      `f32 next_beat`.

Every target index in the 25-file inventory is in the same clip-index domain.
All files end immediately after the last node. The inventory spans 1 to 81
clips per graph and 1 to 4,439 nodes, so the contract covers both minimal UI
graphs and full performer graphs. Packed-byte accounting proves the framing;
the later `CharGraphNode`/`CharClip::Transitions` implementation supplies the
source/target and beat semantics. `tools/acg` supports exact no-edit round
trips, deterministic edits, target-index validation, and truncation rejection.

## GH1 ACS manifest contract

The packed `charsys/anims.acs` and `arena/anims.acs` files are ASCII
precache manifests, not opaque binary clip data. Their complete observed
grammar is:

1. blank lines;
2. semicolon comment lines;
3. `#include <relative .dta path>` lines;
4. bare macro-invocation symbols, one per line.

Line endings, whitespace, comments, and suffixes are retained independently
from editable include/invocation values, so `tools/acs` round-trips both files
byte-exactly. Compiled include lookup inserts `gen` below the ACS directory
and changes `.dta` to `.dtb`: `charsys/anims.acs` therefore resolves
`anims_macros.dta` to `charsys/gen/anims_macros.dtb`, and the arena manifest
resolves to `arena/gen/anims_macros.dtb`. Both targets exist in the packed
GH1 archive. The manifests contain 30 character/band invocations and one
arena crowd invocation respectively.

The shared DTB preprocessor resolves nested includes and bare-symbol aliases
recursively, with cycle detection. Expansion yields 31 clip-set records:
eight main guitarist sets, eight UI sets, eight combined hand sets, five band
sets, one wing set, and one crowd set. They account for all 926 ACPs exactly
once.

## GH1 ACP to GH2 CharClipSamples mapping

The mapping is now corpus-proven and independent of clip or character names:

1. remove the standalone wrapper and revision-18/revision-5 gates;
2. clear ACP bit 31, which is an export marker rather than a gameplay flag;
   the lower 31 bits equal the expanded authored GH1 flags across the
   inventory. For the guitarist-main role, remove the GH1 clip-family
   membership mask `0x063FC0E0` after constructing the corresponding
   `CharClipGroup` rows. GH2 stores that membership in the groups, and several
   GH1 membership bits collide with unrelated GH2 meanings. The mask is
   derived from the compiled family table, not from clip or character names;
3. translate GH1 beat-align 1/2/4/8 to GH2
   `0x1000/0x2000/0x4000/0x8000`, real time to `0x0200`, and GC-lock to
   GH2 user time `0x0400`;
4. map source set 0 to GH2 `full`, retaining multi-sample and virtual facing
   channels;
5. map source set 1 to GH2 `one`, retaining one-sample constants;
6. write GH2's revision-8-through-12 third legacy header without a third data
   block. The loader discards this header, so generated files use an empty
   deterministic header and never duplicate sample bytes;
7. group ACG nodes by target clip in first-target order while preserving node
   order within each group.

The sweep contains 782 populated set-0 bodies, 144 empty set-0 bodies, 926
one-sample set-1 timelines, zero channel overlaps, and 623 clips with exactly
two facing channels in set 0. Every generated revision-10 CharClipSamples body
reparses and reserializes exactly.

GH2 revision-10 `CharClipSamples` also serializes two legacy script fragments
(enter and exit) before its counted beat-event vector. These are not animation
names or optional diagnostics: all 55 clean-retail crowd clips carry a
nonempty enter event which calls the character's authored `set_hand` handler.
The split is 23 clap, 14 fist, 11 devil, and 7 lighter events, and at least one
clip (`male02_1armpump_02`) disproves a spelling-based selection rule by
requesting `devil`. No-edit GH2 reads/writes must preserve the two strings and
every `(beat, script)` row. A newly converted GH1 ACP has no equivalent fields,
so the deterministic GH2 target values remain empty unless an independently
decoded GH1 source supplies equivalent events; the converter must not
synthesize them from clip, character, mesh, or venue names.

Runtime dispatch is likewise data-driven. `char/gen/char_objects.dtb` is
preprocessed with its archive-relative includes and its serialized
class/superclass graph is followed: a `BandCharacter` whose root type is
`crowd` resolves the handler under `Character/types/crowd`. The exact clip
event is reparsed as DTA and executed against decoded named objects. Retail's
lighter state is left-clap plus right-lighter with `lighter_flame` started;
monkey/eye heads remain cheat-controlled. Evidence and the complete 55-clip
ledger are in
`proofs/gh1-native-conversion-parity/worldcrowd-script-contract/`.

Static PS2 analysis establishes generated summary allocation values.
`CharClipSamples::AllocSize` combines a fixed `0x3a0`, transition storage, and
aligned CharBonesSamples allocations. Vector runtime slots are 16 bytes,
compressed quaternions 8, compressed scalar rotations 2, and each sample
stride is 16-byte aligned. GH2 reads and discards the two legacy summary
integers in a CharClipSet; no-edit target round trips preserve them, while new
assets use the deterministic PS2 runtime value.

## Native GH2 character clip-set packaging

`tools/milo_convert` emits complete revision-24 `CharClipSet` directories.
The 31 source records become 39 packages because each of the eight combined
GH1 hand bundles is partitioned by its authored external anchor into separate
fret and strum packages. The packages include:

- revision-14 roots with established GH2 role/type versions;
- revision-10 clips and converted ACG transitions;
- revision-2 CharBones derived from zero-geometry archetype meshes, effective
  transform inheritance, and channel contexts;
- external instrument anchors required by the GH2 fret/strum contract;
- CharClipFilter objects, flag-derived guitarist groups, and authored
  venue-exclusion groups;
- the seven standard GH2 authoring viewports and deterministic MILO_B
  containers.

All authored recenter targets and averaging bones are retained. `move_self`
is not assigned from an asset or role name: an authored true value and the
facing-driven guitarist contract take precedence; otherwise it is true only
when every recenter target has a multi-sample position channel in every clip
in that partition. This reproduces the target distinction evidenced by the
retail bass set (pelvis and carried-instrument targets always in `full`,
`move_self=1`) and singer sets (the fixed microphone target enters `one` in
stationary clips, `move_self=0`). The GH1 sweep independently exhibits the
same channel-set distinction.

The archetype sweep classifies all 1,307 channel-base occurrences: 1,275
resolve directly to source zero-geometry skeleton meshes, 16 are the authored
virtual facing pair, and 16 are the authored external fret/strum hand anchors.
There are zero unresolved channel bases; the audit now rejects any future
unclassified base instead of reporting these two deliberate domains as
missing skeleton nodes. All 39 packages and all 926 clips reparse through
GH2-native readers and reproduce their directory/container bytes on an
independent second conversion pass. The machine-readable ledgers are
`proofs/gh1-native-conversion-parity/semantic-field-contract/conversion-audit.tsv.skeletons.tsv`
and `conversion-audit.tsv.packages.tsv` beside it.

## Character manifest and GH2-native controller bodies

The shared generic DTB preprocessor now compiles the packed
`charsys/gen/charsys.dtb` character definitions rather than relying on a
character-name table. It resolves includes, merges, macros, and `HX_EE`
branches and extracts the authored outfit directory, skeleton load and
compiled path, LOD thresholds, sphere base, servo channels, driver realign
flag, twist/IK controller declarations, eye setup, walk setup, and face file.
The packed result is 13 character definitions: eight guitarist archetypes and
five band performers. It creates 79 authored controller declarations,
including Grim's wing servo/driver, two knee-to-cloak rods, and position
constraint. It also resolves the authored face and shadow package paths.
Every one of the 13 compiled skeleton RND paths and every declared auxiliary
package exists in the GH1 archive.

The clean GH2 target audit now has semantic readers and writers for every
character/controller family required by those manifests and their immediate
stock package dependencies. The following counts are whole-archive,
revision-24 object-body round trips against the clean retail target:

| Target body | Observed revision | Exact bodies |
| --- | ---: | ---: |
| `CharDriver` | 3 | 83 |
| `CharDriverMidi` | 3 | 76 |
| `CharEyes` | 3 | 19 |
| `CharForeTwist` | 1 | 118 |
| `CharHair` | 2 | 93 |
| `CharIKHand` | 2 | 76 |
| `CharIKMidi` | 4 | 38 |
| `CharIKRod` | 2 | 6 |
| `CharLookAt` | 2 | 38 |
| `CharPosConstraint` | 2 | 11 |
| `CharServoBone` | 1 | 81 |
| `CharUpperTwist` | 1 | 136 |
| `CharWalk` | 1 | 38 |
| `CharWeightSetter` | 2 | 76 |
| `EventTrigger` | 8 | 298 |
| `FaceFxLipSyncServo` | 5 | 42 |
| `OutfitLoader` | 1 | 40 |
| `WorldFx` | 1 | 193 |

All rows above consume the complete body and reserialize byte-for-byte with
zero failures. Important nested contracts are:

- `CharWeightable` revision 2 is `f32 weight` followed by its owner reference.
- `CharDriver` revision 3 is Object, Weightable, bones reference, clip-set
  path/reference, and one-byte realign flag.
- `CharHair` revision 2 stores six simulation floats, a strand vector, and a
  simulate byte. Each strand stores root, angle, points, and two 3-by-3
  matrices. Each point stores position, bone, length, the revision-2 legacy
  integer/string pair, radius, and outer radius.
- `EventTrigger` revision 8 stores its single trigger symbol, animation rows,
  sounds, shows, the legacy hide list, enable/disable/wait event lists, next
  link, and proxy calls. The legacy hide list is the revision-8 branch of the
  later hide-delay representation; omitting it leaves four residual bytes even
  when the list is empty.
- `FaceFxLipSyncServo` revision 5 is Object, Weightable, FAC path, viseme MILO
  path, and target rows of object reference, property-type integer, and
  property symbol.
- `WorldFx` revision 1 contains a complete nested `RndDir` revision-8 body.

`OutfitLoader` revision 1 was recovered from the clean PS2 executable as well
as the packed bytes. Its save routine at `SLUS_214.47:0x0018AEC8` writes:

1. revision 1 and Object fields;
2. the loader directory `FilePath`;
3. a `u16` category count;
4. for each category, one-byte selected and shown states plus a `u16` outfit
   count;
5. for each outfit, one-byte hide, desire, and exclude states.

The reciprocal loader at `0x0018B170` validates the serialized category and
outfit counts against the type-definition-created vectors before applying the
states. This exactly accounts for both clean target layouts: the guitar type
has one category with 58 outfits and a 214-byte body; the drummer type has one
category with eight outfits and a 65-byte body. The corresponding authored
type definitions were independently decoded from `char/gen/char_objects.dtb`.

## GH2-native character model package sweep

The model-package builder now converts all 13 packed GH1 character definitions
without consulting a character-name table. The eight selectable guitarist
archetypes become `BandCharacter` directories; the two singers, bassist,
drummer, and keyboardist become `Character` directories. LOD groups come from
the authored `lodN` view graph, and old-revision thresholds are normalized by
the derived or authored bounding-sphere radius as required by the target
`Character::PostLoad` contract. Each package is generated twice, produces
identical directory/container bytes, reparses with exact boundaries, and
serializes back to the same bytes.

The GH2 target does not retain the GH1 skeleton as zero-geometry `Mesh`
objects. Recovered `Character::SyncObjects` source calls
`ConvertBonesToTranses(this, false)` when the character directory contains the
exact object `bone_pelvis.mesh`. Recovered `ShouldStrip` selects
case-insensitive `bone_` and `exo_` prefixes and the case-sensitive `spot_`
prefix. The converter now applies that same directory-wide rule: each selected
GH1 Mesh28 record is decoded and rewritten as a GH2 Trans9 record while
preserving its object metadata, local/world transforms, constraint, target,
preserve-scale state, and parent reference. Names and references stay stable;
only the target object type changes. The complete generated sweep loads with
29-78 native transforms per character instead of a zero-bone compatibility
graph.

GH1 character-shadow packages are separate revision-10 RND directories. Their
View graph may contain nested helper Views, so the converter finds the unique
unreferenced root and walks the complete reachable graph. Only meshes
reachable as drawables are merged; duplicate transform-only skeleton meshes
are intentionally not merged. Shadow drawables retain their authored
bone-slot offset matrices and refer to the primary character skeleton. This
matches the native target arrangement: clean GH2 character packages contain
shadow drawables but no duplicate shadow skeleton, and
`Character::SyncShadow` wraps each shadow-mesh bone reference around the
primary transform. All merged groups and drawables receive deterministic
role-based names, with group, geometry-owner, parent, and drawable references
rewritten together.

The target character root contract is decoded and serialized exactly, not
inferred from resident filenames. `Character9` stores revision 9, an embedded
`RndDir8`, a counted sequence of `(screen_size, group_ref)` LOD rows, the
shadow-group reference, one self-shadow byte, and the sphere-base reference.
`BandCharacter1` adds its revision-1 wrapper before that same Character9 body.
The engine now consumes these fields through the same `milo_object` parser as
the converter. The active LOD comes from the decoded root reference, and the
ordinary character pass excludes only meshes transitively reachable from the
decoded root shadow group. The previous `top.view`/`lod0.grp` and `shadow*`
spelling tests, including the attached-prop shadow-name skip, are removed.

The decoded root also governs positive draw ownership. A native character
directory can retain transform/helper Mesh objects which are not members of
its authored draw graph. Both GH1 singer packages demonstrate the distinction:
their unmaterialed `Bip01 Ponytail*` helper meshes are resident for skeleton
semantics but are not reachable from the character's root View graph.
Appending all ungrouped meshes rendered those helpers as white cards behind
the singers' heads.

Runtime now starts from the selected `Character9` LOD group, finds its decoded
ancestor groups, and traverses the outer authored closure. Other serialized
LOD branches are suppressed, while direct siblings under the ancestor—such
as the singers' microphone stand and earrings—remain visible. Resident meshes
outside that closure are not implicit draw roots. The rule uses only decoded
root and group references; it does not inspect character, ponytail, mesh,
material, or empty-material spellings. Focused tests cover active/inactive LOD,
authored accessory, and ungrouped-helper outcomes. Fresh unfiltered rear
captures of both deployed singers remove the white helpers. The current
deployed build also has a 6.1-second input-free side-by-side motion proof:
both source-authored active clips run under a fixed rear camera through the
ordinary unfiltered draw path, and neither singer submits the helper geometry.
Evidence:
`proofs/gh1-native-conversion-parity/singer-rear-head-contract/`.
The same deployed executable passes a hidden, input-free, strict-native load
sweep of all 13 converted GH1 character packages with zero engine failures;
the evidence folder contains the per-package raw logs and summary matrix.

FaceFx animation-package resolution is likewise field-driven. Stock GH2
`FaceFxLipSyncServo5` rows serialize the exact relative `viseme_milo`; the
deployed female and male singer rows name `../../anims/female_viseme.milo`
and `../../anims/metal_viseme.milo`. The shared FaceFx resolver converts that
reference into source/compiled and non-`gen`/`gen` candidates for both
gameplay and the character viewer. The former model-stem-derived
`*_viseme.milo_ps2` fallback is removed. Converted GH1 singers serialize no
FaceFx servo because their Morph/EventTrigger face programs use the decoded
GH1 face-event path, so an empty servo list now produces no guessed package
lookup. Focused path tests and hidden strict-native GH1/GH2 singer loads are
recorded in
`proofs/gh1-native-conversion-parity/facefx-serialized-viseme-path/`.

GH1 face packages contain revision-3 `Morph` objects plus their pose meshes.
The converter upgrades them to revision 4, identifies the unique frame-zero
basis pose from the one-hot curves, and binds each morph to the single primary
character mesh having the same vertex count and exact face-index topology.
Pose meshes and their references are then namespaced and merged. This works
across all eight guitarist faces and both singer faces without asset-specific
target names. GH1 selects authored pose frames from excitement and MIDI face
events, whereas stock GH2 normally drives facial bones through
`FaceFxLipSyncServo`, FAC actors, and viseme clip sets. The converter therefore
builds a native Morph/event graph instead of pretending the stock FaceFx input
registers are Morph outputs.

The pose meshes also retain a source ownership distinction that is easy to
lose during packaging. In GH1 they live in a separate face `RndDir`; they are
geometry donors referenced by `Morph`, but they are not members of the
character model's drawable LOD closure. Their local `RndDrawable::showing`
value therefore does not make them visible beside the live model. Combining
the face directory with a GH2 character package changes that ownership
boundary: leaving the donor bit untouched caused every facial pose—including
an alternate teeth pose—to render simultaneously on Johnny Napalm.

`merge_face_model` now preserves every donor mesh and Morph reference while
serializing all merged face-pose donors with `showing=false`. This is a format
rule for every GH1 face package, not a Johnny/teeth name check or transform
offset. The focused merger test requires both reference and expression donors
to remain present and hidden. In the regenerated Johnny package the native
inventory changes from 158/158 showing meshes to 138/158: exactly the twenty
face donors cease drawing while the authored live head and teeth remain.
The full conversion sweep still passes 105/105 assets, 926/926 animation
clips, and 13/13 character packages with zero blocked rows. Native before/after
images, logs, source inspection, and the regenerated-package audit are under
`proofs/gh1-punk-face-fix/`.

The runtime Morph reader now accepts both source revision 3 and target revision
4. Revision 4 consumes the native Hmx::Object base and revision-4 Animatable
frame/rate fields before the same pose/key payload. A converted Small Club
native-only run loads all guitarist and singer Morphs with zero unsupported
revision diagnostics; retaining a revision-3-only decoder would have hidden a
real target-loader gap behind successful package conversion.

The character manifest now preserves the complete authored face-control
contract instead of only the face package path. The eight guitarist rows each
declare ten poses, five excitement-state pose sets, a 0.5-second blend, a
1.0-second random-pose interval, and hero MIDI mappings
`113 -> good01` / `114 -> good02`. Both singers declare `ref` and `open`, a
0.1-second blend, the `singer` event list, event offset 257, and
`108 -> open`. The apparent missing guitarist `event_list` is not an unknown:
the GH1 executable initializes it to `hero` before applying an optional
authored override. The preprocessed excitement symbols are the exact numeric
states 0 through 4 defined by the packed system macros.

The stock GH1 controller algorithm has also been recovered statically from
`SLUS_212.24`, rather than inferred from screenshots:

- `0x00287378` acquires `face_data` and `excitement_poses`;
- `0x00287490` loads `blend_time`, `pose_length`, and `event_offset`;
- `0x002878C0` resolves the effective event list as `singer` or `hero`;
- `0x00287A80` applies the event offset, selects the active MIDI face event,
  and resolves its pose symbol;
- `0x00287B70` advances the random-pose timer and selects a different pose
  when the authored pose length expires;
- `0x00287C78` maps a pose symbol to its zero-based index in the authored
  `poses` row, which is the Morph frame;
- `0x00287DD8` through `0x00287F38` crossfade the previous and destination
  frames over `blend_time` with the exact half-cosine weight
  `0.5 - 0.5*cos(pi*t)`.

This establishes that a faithful target graph must preserve discrete pose
selection and crossfade two evaluated Morph frames. Sweeping continuously
through the intervening frame numbers, treating FaceFX eye-input rows as
Morph outputs, or substituting a fixed expression would all be incorrect.
The converter now translates that contract into stock GH2 primitives. For
each ordered source/destination pose pair and each authored Morph target, it
emits a revision-4 Morph containing only those two pose meshes. Its keys sample
the exact `0.5 - 0.5*cos(pi*t)` weights at GH2's native 30-fps update frames,
so a nonadjacent transition never evaluates any intervening authored pose.
The pair Morphs are collected in a revision-12 Group, driven over the authored
blend duration by a revision-1 `AnimFilter`, and launched by a revision-8
`EventTrigger`. Self-pairs materialize and retain a single pose without
duplicating it.

The packed sweep produces 808 complete transition graphs: 100 for each of the
eight ten-pose guitarists and four for each two-pose singer. These graphs are
generated from the manifest pose rows and Morph topology; no package, mesh, or
pose-name case table participates in graph construction.

The converter also supplies a deterministic patch for the clean target
`../../system/run/milo/gen/rnd_objects.dtb`. It adds generic
`gh1_guitarist_morph_face` and `gh1_singer_morph_face` Group types without
replacing any stock Group schema. Converted face packages expose their
controller as `lip.servo`, allowing the existing stock `BandCharacter`
messages to reach the new target-native scripts. The guitarist type reproduces
the authored boot/bad, okay, great/peak pose pools, the one-second pose timer,
no-repeat selection, target expression-event mapping, override state, and
resume behavior. The singer type exposes exact `ref`/`open` transition
messages. `OwFace` has no GH1-authored pose and is intentionally not mapped to
a fabricated shape.

The DTA source for those rows is compiled by the new general DTA parser rather
than patched into DTB bytes by offsets. The parser supports Harmonix atom
types, `()`/`{}`/`[]` collections, strings, variables, numeric values, escapes,
line comments, and line metadata; the patched encrypted DTB reparses and
reserializes byte-for-byte. The clean 37,159-byte target file becomes a
51,346-byte deterministic file with the same 39 roots.

Singer song drive is now translated as data too. The `.voc` reader accepts the
two packed FACE archive revisions, 1200 and 1500, and accounts for their
14-byte and 36-byte revision-specific footers. It unions the positive
piecewise-linear support of the 15 non-neutral viseme curves into exact open
intervals. The SMF reader preserves HMX running status across Meta and SysEx
events, evaluates the complete tempo map, and emits a deterministic appended
`GH1 SINGER FACE` track: zero-length pitch 108 events open the mouth and
zero-length pitch 109 events close it. The original MIDI byte prefix is
unchanged; only the header track count and appended `MTrk` are new.

The full clean-target sweep parses 59 `.voc` files: eight revision-1200 and 51
revision-1500 archives. Fifty-eight have paired song MIDIs and produce 1,283
open spans; the remaining `_blinktrack` archive has no song MIDI and is
reported rather than fabricated. Focused real fixtures cover both revisions:
`HeartShapedBox` produces 17 spans and `YouReallyGotMe` produces 14.

Two additional deterministic target config patches complete the event route.
`midi_parsers.dtb` gains a `gh1_singer_face_parser` for the generated track;
pitch 108 sends `singer_face_open` and pitch 109 sends
`singer_face_close` through the existing `singer_parser` `MsgSource`.
`char_objects.dtb` adds matching handlers to the stock singer `Character`
type, forwarding them to its local `lip.servo`. This follows the target
message-source contract: an unhandled message is exported to subscribed
sinks. The generated encrypted outputs reparse and reserialize exactly:
`gh1-face-midi_parsers.dtb` is 20,335 bytes with 28 roots and
`gh1-face-char_objects.dtb` is 46,976 bytes with 23 roots.

The clean-target `FaceFxLipSyncServo` rows are not a hidden Morph bridge.
Across all 42 clean bodies there are 72 target rows, and every row is one of
four eye inputs: property type 0 for `L-eyeX`/`R-eyeX` or type 2 for
`L-eyeZ`/`R-eyeZ`. The target routine at
`SLUS_214.47:0x001901D0` switches only on values 0, 1, and 2, reads the
corresponding transform rotation component from the referenced object, and
feeds that scalar into the named FAC graph node. FAC/viseme setup occurs at
`0x0018CE18`, and the runtime evaluation path begins at `0x0018D3F8`.
Consequently, assigning a Morph object to a stock target row would be
incorrect; a native event/animation control path or a complete equivalent FAC
graph must be recovered.

## GH1 eye-controller lowering

All eight authored GH1 guitarists use the same `create_eyes` facts:
`parent=bone_head.mesh`, `constraint=0.925`, and `lid_lower=0.5`. These values
are compiled into the character manifest rather than inferred from mesh names.
The controller implementation was recovered from `SLUS_212.24`:

- `0x0018DBF0` allocates the 0x110-byte legacy controller;
- `0x0018FE18` resolves `L-eye.mesh`, `R-eye.mesh`, `parent`,
  `constraint`, and `lid_lower`;
- the constraint is stored at `+0xF4`, while
  `sqrt(1 - constraint^2)` is stored at `+0xF0`;
- `0x00190110..0x001906B4` uses those two values to clamp the desired eye
  direction to the authored circular cone;
- the 0x1C-byte helper at `+0xD4` is initialized from `lid_lower`, zero,
  0.075, and 0.5, but is never read or passed by the controller's poll,
  start, or stop paths. Its constructor only initializes its local fields and
  vtable; it does not register the helper with a scheduler.

That last point accounts for `lid_lower` without inventing a target behavior:
it is authored but dormant in the shipped GH1 implementation. GH2
`CharEyes` revision 3 cannot serialize lid descriptors anyway; its old-load
path contains only the `CharLookAt` list and one legacy transform reference.

The native lowering creates two revision-2 `CharLookAt` objects and one
revision-3 `CharEyes`. The eye mesh references come from exact resolved
directory entries, each eye mesh must already have the authored parent, the
empty target is the stock GH2 `CharEyes::NextLook` route, and half-time is
zero because the GH1 poll has no temporal smoothing. The source cone cosine is
converted systemically to symmetric target limits:

`limit_degrees = acos(constraint) * 180 / pi`

For the packed value 0.925 this is 22.3316 degrees on yaw and pitch. The
converter rejects a non-finite/out-of-range cosine, a cone wider than GH2's
80-degree `CharLookAt` representation, a missing parent, or an eye mesh whose
decoded parent disagrees with `create_eyes`. All eight packed guitarist
packages pass these checks; no eye-side, character-name, or offset-specific
rule participates.

Grim's two authored knee-to-cloak rigs now use the target
`CharIKRod::SyncBones` contract directly. The converter interpolates the two
authored endpoint world transforms, constructs the vertical/side-axis rod
frame, and serializes
`destination_world * inverse(rod_frame)` as the controller offset. The result
independently matches the retail GH2 Grim offsets to the source-export float
precision; for example the generated right-knee translation is
`(-2.49629,-0.127955,0.125637)` versus the retail port's
`(-2.49618,-0.127943,0.125631)`. No controller-specific offset is stored.

The packed sweep currently produces 13 deterministic model packages. The
three performers without authored face packages have no unresolved model
dependency; the ten face-bearing packages report only that their generated
face-control configuration must be installed beside them. The corresponding
Rnd, MIDI-parser, and Character config outputs and singer MIDI translation are
now deterministic.

The loose bundle builder owns that dependency explicitly. It emits 13 model
MILOs under `char/<package>/og/gen`, the 38 animation MILOs owned by those
character archetypes under `char/<package>/anims/gen`, and the three patched
target configs under their native `system/run/milo/gen`, `config/gen`, and
`char/gen` paths. The remaining generated animation package belongs to the
crowd archetype and is deliberately reserved for the venue/crowd bundle rather
than assigned to a character by a name rule. A sorted 54-row manifest records
every character/config output and its source fact. The separately completed
singer-face sweep can add 58 translated song MIDIs at their native
`songs/...` paths, but those song-specific files are outside the final
MILO/DTB/ACP bundle accounting.

Two complete final rebuilds produce identical hashes for every character
output. Model inventory is 13 complete, zero incomplete; face-bearing model
rows list their generated config requirement separately from unresolved
dependencies. Native runtime proof loads all four performer roles from the
converted GH2-layout packages.
Every generated root also now carries the stock GH2 runtime type implied by
the authored GH1 role: `guitarist`, `singer`, `bassist`, `drummer`, or
`keyboardist`. This is a role-schema translation, not a package-name rule.

Instrument integration follows the target contract rather than attempting to
reinterpret GH1 instrument objects. Every guitarist package now carries the
stock GH2 `guitar.outfit` loader (`../../../og`, type `guitar`, one 58-row
category), `fret.ik` targeting the external `bone_fret.mesh`, and the
`left.weight`/`right.weight` setters targeting `main.drv` with flags
`0x00400000`/`0x00800000`. The drummer package carries the stock
`drums.outfit` loader (`../../../og`, type `drummer`, one eight-row category
with selected/shown states 1/1). These are role-derived target-schema facts
from `char_objects.dta` and the exact clean target bodies, not character-name
rules. The unexplained empty `crash_static.filt` placeholder is deliberately
not synthesized: instruments are cursory and no source fact establishes that
the placeholder is required for native character loading.

Every generated internal reference is parsed from its owning target object
schema and resolved by name before serialization. The audit now covers both
transform target/parent slots plus the reference fields in camera, flare,
light/environment, transform/material/mesh/particle animation, multi-mesh,
movie, font/text, particle bounce/emitter/relative-parent, hair, FaceFx,
WorldFx, trigger, material, mesh, group, Morph, and character-controller
bodies. File paths, event symbols, and discarded legacy strings are not
misclassified as in-directory object references. Focused synthetic coverage
independently exercises nested
shadow-root discovery, unreachable duplicate-skeleton omission, role-based
shadow reference rewriting, exact-topology Morph binding, and dangling
reference rejection, including TransAnim and particle owner/target edges. It
also verifies the two-pose transition graph, exact
three-frame half-cosine samples, filter/trigger wiring, generated controller
type, DTA compilation, and patched-DTB round trip. The packed sweep accounts
for 16,637 internal references
with no
dangling object, material, texture, geometry-owner, parent, bone, group, LOD,
shadow, morph, or controller reference. The standard GH2 instrument hand/fret
anchors and animation-package paths are the only intentional external
references.
The machine-readable ledger is
`tools/milo_convert/build/gh1-to-gh2-conversion-audit.tsv.models.tsv`.

## Native venue, script, archive, and runtime closure

Venue conversion is a persistent namespace translation, not a runtime cache.
Each retail `venues/<name>/gen` directory becomes
`world/gh1_<name>/gen`. The prefix preserves GH2 as the authoritative world,
gameplay, HUD, and UI corpus and prevents collisions such as GH1 and GH2 both
owning an `arena`. The venue manifest contains 62 rows:

- 33 converted revision-24 MILO directories;
- 21 support files, including each venue's campaths, sequence,
  sound, and event data;
- seven compiled native venue scripts; and
- the one shared native crowd-animation package.

The seven script conversions contain 886,740 bytes and zero blocked handlers.
The compiler lowers GH1 object handlers, state variables, delayed tasks,
section/excitement messages, `switch_anim`, `switch_anim_rt`, and object
messages into deterministic target DTBs. Retail executable evidence fixes the
animation timing contract: `switch_anim` is at `SLUS_212.24:0x0016C6A0`,
`switch_anim_rt` at `0x0016C828`, and their shared helper at `0x0017A1D0`.
Normal timing is 480 frames per beat with blend divided by 480; real-time
timing is 1,000 frames per second with blend divided by 1,000. Scale-only
messages retain the current range/type. No venue or animation name selects a
different lowering rule.

All seven main venue roots are now promoted from the converted RndDir to the
established GH2 WorldDir11/PanelDir2/ObjectDir16 inheritance chain. The 10
authored GH1 `load_section` directives resolve to real packed lighting/crowd
RndDirs and become native ObjectDir subdirectory links. The root uses the sole
authored GH1 preview Cam, GH2's retail one-player HUD-preview and `ui_enter`
PanelDir defaults, and a neutral editor-preview transform where GH1 has no
WorldDir field. All eight native GH2 main venues provide the target reference
contract; ihatecompvir MiloLib provides the target load/save order.

GH1 also passes finite `switch` expressions as `foreach` collections. The
compiler preserves the dynamic selector while distributing the loop body into
each concrete branch, so target objects come only from authored branch values;
the `switch` syntax and selector variable never become bogus object targets.

Native Group fixup classifies every member by the target directory type and
rebuilds the animatable-child graph before events run. Group animation
recursively propagates to transform, material, environment, light, and
particle animations. A source-present empty Group and a channel-empty MatAnim
resolve as documented no-ops, rather than dangling targets. Direct EnvAnim and
LightAnim targets use the same event route. The `finish_loading` section is a
load barrier: latched intro/music/excitement events replay only after the
lighting directory exists.

The native Animatable0 compatibility contract is also closed for standalone
MatAnims. The complete source audit covers 1,866 legacy Animatable0 bodies:
330 carry operations, 229 carry child references, and those bodies contain
1,147 child references. Shipped GH1 uses only operation type 0
(261 scale/offset rows) and type 1 (248 range/loop rows).

Packed Small Club `smoke.matanim` contains scale `0.00120000006`, offset `0`,
loop range `0..100`, and two texture-translation keys spanning that range.
Small Club `record.matanim` and `record_g.matanim` each contain scale `0.005`,
loop `0..100`, and a Z rotation from zero to pi. Theatre `tram.mnm` contains
scale `0.2`, loop `0..100`, and two X-translation channels but no material
target. These four venue MatAnims have no child object, View owner, PollAnim,
EventTrigger, or venue-script symbol reference.

Retail `SLUS_212.24` proves that this is not an implicit auto-play contract.
`RndAnimatable` load at `0x001AC0C0` reads a counted filter list followed by
its counted child-Animatable list. `0x001AC640` creates the five legacy filter
types, and `0x001ABAB8` applies that chain only after an external caller
supplies an input frame, then recursively forwards the filtered frame to
children. None of these paths creates a task or reads TaskMgr time.

The converter therefore emits each source MatAnim plus a native AnimFilter
which preserves the exact scale/range/loop program. This matches the recovered
later-engine compatibility loader. `RndDir::SyncObjects` retains an unowned
filter in the root list and removes its MatAnim target through
`RndAnimFilter::ListAnimChildren`, but root discovery does not schedule
playback. The deployed GH2 audit independently finds 1,698 native world
Animatables, 401 AnimFilters, 588 root candidates, and zero PollAnim objects;
its Group, filter-target, and EventTrigger relationships are recorded
separately. No automatic song-time owner is synthesized for unowned GH1
objects. Evidence:
`.codex/analysis/gh1-native-matanim-scheduling/` and
`.codex/analysis/gh2-native-filter-audit-2026-07-29/report.tsv.anim-ownership.tsv`.

Every GH1 VenueCam record is converted to a native revision-20 GH2 CamShot.
Camera path, offset, singer-screen compensation, FOV, timing domain, ease,
shake, and supported behavior fields are evaluated into native target keys;
the source `camera.dtb` is not shipped beside those objects. Native CamShots
are authoritative at runtime, with the old DTB reader retained only as a
legacy fallback when no native camera keys exist.

The default single-player target-position resolver at
`SLUS-21224:0x0016E080` selects ArenaSinger slot zero (the player guitarist);
its virtual at `0x0018D3C0` looks up `bone_head.mesh`. This function was
previously mislabeled as the complete camera update. The 2026-09-03 audit
found that `arena/gen/cam_paths.dtb` overrides parent and target per path.
The converter now consumes that source policy instead of emitting one blanket
pair. Explicit stage/singer/guitar-neck refs and default translation-only
player-head parents are distinct. Conditional guitar-reference fallbacks are
retained in TypeProps for runtime resolution against the actual character.
`singer_in/out` are historical framing-field names, not vocalist selection.

CamShot `ObjectFields` use the real HMX `TypeProps` wire contract recovered
from `TypeProps.cpp`: one flat DataArray whose roots alternate
`Symbol key, DataNode value`. A value may itself be an array, but a property
is not wrapped in a nested pair array. Converter output, the object inspector,
and runtime parsing all use that contract; the runtime accepts the earlier
nested-pair form only to read old generated loose assets.

Target rendering also preserves the source submission boundary. A Mesh with no
material is a transform/editor helper and is not submitted by the platform
renderer. This rule removes Arena's two large helper planes without naming
those meshes or hiding geometry. Tex8-to-Tex10 retains the same-platform PS2
bitmap bytes, and Mat21 stages become linked Mat27 `nextPass` objects, so
texture alpha, blend, palette, swizzle, and stage order are not reconstructed
from screenshots.

GH1 View conversion also preserves two distinct source graphs. Revision-7
`View` carries animation/message children separately from revision-1 Drawable
children. GH2 revision-13 `Group.draw_only` is the native bridge: the primary
Group retains every animation target and points at a generated companion
Group containing only the recursively expanded source drawable closure.
Nested Views stop that recursion and retain their own boundary. Drawable
objects not reachable from the authored root View are retained for references
and scripts under a hidden owner Group, preventing native RndDir root
discovery from promoting editor/helpers into independent draws.

Small Club is the matched structural proof. Raw and converted main directories
submit 181 meshes from 211 grouped objects; raw and converted lighting
directories submit 145 meshes from 241 grouped objects. Its hanging records
also prove the material path: Mat21 authors `alphaCut=true` and `cull=false`,
while each retained indexed PS2 bitmap contains transparent and partial-alpha
pixels. The target renderer consumes decoded alpha-cut and threshold state
generically, so the circular silhouette appears without an asset-name or
black-key rule. The paired `record*.1.mesh` objects are not rear cards: all
21 attached instances are four-vertex/two-face suspension strips parented to
their corresponding record. Their longest extent is exactly 44.683188 source
units and their middle extent is only 0.201836--0.281165 units. A complete
raw-to-native comparison reports zero vertex, normal, shared color-slot, UV,
face, or material mismatches for those strips.

The complete bundle is described by two manifests:

| Manifest | Rows | Payload bytes |
|---|---:|---:|
| `gh1-character-bundle.tsv` | 54 | source-derived payload set |
| `gh1-venue-bundle.tsv` | 62 | source-derived payload set |

Including the two manifest files, the unique deployable set is 126 files with
39,275,873 payload bytes and 39,288,083 bytes including the manifests. Two
consecutive clean conversions match all 124 payload files and both manifests
byte-for-byte. Both sorted
`relative_path<TAB>file_sha256<TAB>file_size` ledgers produce SHA-256
`819FCE3A740A4E7619C493653984A507D0F08EB320D40FE630A9979F06FB287B`.
Evidence is in
`proofs/gh1-native-conversion-parity/deterministic-native-bundle/`. The
pre-WorldDir overlay verification is superseded. The refreshed manifests are
deployed, the immediate second overlay appends zero bytes, and the archive
retains exact ARK v3 framing.

The packed audit closes 105/105 source directories and 926/926 ACPs:
13,115 converted objects, 793 source-derived target objects, zero blocked,
13/13 character packages, 79 controllers, 31 animation sets, 926 animations,
25 ACG assets with 29,808 nodes, 33 venue assets, and 14,696 venue references.
Venue camera conversion emits 201 native CamShots containing 191,320
keyframes with zero blocked records. Every generated target names
`guitarist0:bone_head.mesh`, matching the retail ArenaSinger virtual rather
than a body-anchor inference. The seven-venue native runtime sweep
loads native world, script, and lighting paths for every venue with zero
legacy raw-script loads, unsupported operations, or unresolved targets.

The runtime now also has an explicit `--require-native-assets` proof gate. It
disables raw `venues/...`, raw `charsys/...`, legacy VenueCam, and raw root or
sibling section fallbacks. WorldDir-owned drawable geometry remains legal
native RndDir content, and lighting subdirectories are classified by decoded
content (`LightAnim`, `EnvAnim`, `LightPreset`, or `Light` plus `Environ`)
rather than a GH2-only `_lighting.milo` filename. All seven converted venue
roots pass the strict gate with native world/script/lighting paths, four
`layout=GH2` performers, and a final gameplay state. Every row has zero gate
violations or legacy markers. Evidence is under
`proofs/gh1-native-conversion-parity/strict-native-venue-matrix/`; the detailed
Small Club load is under
`proofs/gh1-native-conversion-parity/post-worlddir-native-runtime/`.

All 13 converted BandCharacter roots also pass a strict native-only package
matrix. Each row loads the ordinary `char/<model>/og/gen/<model>.milo_ps2`,
decodes every mesh with zero failures, loads one source-derived clip from its
native animation package, and exits without missing-asset, decode,
unsupported-Morph, or gate-violation markers. This complements rather than
replaces the packed all-926-animation audit. Evidence:
`proofs/gh1-native-conversion-parity/strict-native-character-matrix/`.

Role-owned clip lookup is also identity-independent. The runtime formerly
branched on the literal `female_singer` model and supplied a separate fallback
list. It now derives namespace-qualified candidates from the decoded driver
package and the semantic role: if a namespace ends in that role,
`singer_idle` becomes `<variant>_singer_idle`. A strict native-only mixed run
loads converted GH1 `metal`, `female_singer`, `metal_bass`, and
`metal_drummer` as four `layout=GH2` performers in native GH2 RedOctane while
playing GH2 `shoutatthedevil`; the female package resolves
`female_singer_idle` from its own `main.drv` path and reaches gameplay.
Evidence:
`proofs/gh1-native-conversion-parity/native-role-clip-resolution/`.

The final mixed runtime proof loads `alterna`, `metal_singer`, `metal_bass`, and
`metal_drummer` from ordinary `char/<model>/og/gen/<model>.milo_ps2` paths with
`layout=GH2`, while playing GH2 `shoutatthedevil` in
`world/gh1_small_club`. It starts on native `Intro01`, transitions to the
regular native `flr_near_lft01` CamShot, and finishes 18 seconds of gameplay
without a compatibility-script load, unsupported operation, unresolved
animation target, or performer-load failure. Proof data and the linked video
are under
`proofs/gh1-native-conversion-final/native-format-camera-final/`.

One renderer-side asset exception was removed during the same audit. The
RedOctane `main_hall.mesh` override claimed reversed source winding, but the
deployed retail GH2 package proves determinant `+1`, 219/219 face/normal
agreement, self-owned geometry, and authored `cull=true`. The renderer now
uses only the serialized material cull bit and the determinant of the final
submitted world transform: negative handedness reverses D3D winding for every
mesh, while positive and singular transforms do not. Focused synthetic tests,
the packed RedOctane witness, a native-only GH2 RedOctane run, and a
native-only converted Small Club run all pass. Evidence:
`proofs/gh1-native-conversion-parity/systemic-cull-handedness/`.

### GH2-native transform-controller publication contract

GH1 revision-10 character skeleton nodes are zero-geometry `RndMesh` transform
records. The GH2 target conversion described above changes qualifying
`bone_`/`exo_`/`spot_` objects into native `RndTransformable` records while
retaining their authored names and graph references. A native controller
reference such as `bone_R-hand.mesh`, `bone_R-foreArm.mesh`, or
`bone_R-upperArm.mesh` therefore resolves through one mutable transform
namespace spanning native transforms and the remaining mesh-backed channels.
`CharIKHand`, `CharForeTwist`, and `CharUpperTwist` consume that namespace; the
converted package does not depend on the GH1 zero-bone compatibility layout.

Controller reads must observe the live resident transform after earlier
controllers in the same poll. A rejected 2026-07-27 proof exposed that native
world lookup preferred `runtime_pose_output_worlds`, a pre-controller CharClip
snapshot, over the resident object graph. `CharIKHand` updated the upper arm,
then `CharUpperTwist` read the stale snapshot and displaced both twist helpers.
The corrected lookup keeps explicit runtime overrides first, then resolves the
resident bone/mesh transform, and uses pose-output snapshots only as fallback
for channels that have no resident object. This follows the recovered
`RndTransformable::WorldXfm` poll contract and is independent of character,
side, pose, clip, or object name.

The resident namespace includes attached instruments. GH2 mounts the character
and instrument into one object directory, while the native renderer represents
instrument-only transforms as attached-prop proxies. `AcquirePose` therefore
resolves the character's `.trans`, character bone `.mesh`, character render
mesh `.mesh`, attached-prop `.trans`, and attached-prop `.mesh` in that order.
World-chain reads likewise select the live attached-prop transform before the
nonresident output snapshot fallback. This closes the converted-GH1
`bone_fret_hand` / `bone_strum_hand` publication boundary without any
character, instrument, transform, or offset-specific rule. Focused tests and
the logged strict-native gameplay matrix cover all eight GH1 guitarists, 224
finite/non-origin targets, and 176 full-weight solves with a maximum 1.7035
world-unit reach residual. The packed Hair Metal source independently accounts
for its offstage intro transition. Evidence is in
`proofs/gh1-native-conversion-parity/instrument-hand-parity/shared-objectdir-hand-targets/`.

The same review found a separate presentation error: revision-24 converted
characters drew every authored LOD group because group selection was
incorrectly gated on the GH1 revision-10 directory number. Selection now uses
the exact `(screen_size, group_ref)` rows decoded from the native Character9
root, independent of directory revision and authored object spelling. This
prevents duplicated overlapping arm surfaces and also makes stock GH2 and
converted GH1 characters use the same runtime contract.

The exact GH2 PS2 `CharIKHand::Poll` body in `SLUS_214.47` at
`0x0017A080` materializes `0xBF7C28F6` and `0x3F7C28F6` at
`0x0017A1AC..0x0017A1EC`, clamping the elbow cosine to `[-0.985, 0.985]`.
That differs from the later RB3 source helper's `[-1, 1]` clamp and is applied
only as the recovered GH2 PS2 target contract.

At the same converted Alterna intro sample, the native post-controller
upper-arm matrices now match the raw GH1 compatibility path to six decimal
places for both arms. A second input-free sweep after native-Trans publication
loads 63-78 transforms for every selectable guitarist and 28-34 transforms for
the five band-role packages. It covers all eight guitarists, male and female
singers, bassist, drummer, and keyboardist. The proof includes the exact source
ACP, native/raw phase traces, controller traces, matrix traces, and current
visual captures plus the labeled 39.65-second input-free motion sweep under
`proofs/gh1-native-conversion-parity/instrument-hand-parity/arm-twist-isolation/`.
The authored guitar relationship did not require an attachment correction:
raw and converted-native `attach-world`, `prop-anchor-world`,
`prop-to-attach`, and `target-local` matrices were already identical to six
decimals. Publishing the recovered controllers into mesh-backed transforms
restored the two hands around that exact prop target. Fresh deterministic,
zero-input raw/native gameplay frames and a close converted-native view are
under `proofs/gh1-native-conversion-parity/instrument-hand-parity/`.

### CharDriver clip-directory authority and instrument pose

GH2 revision-3 `CharDriver` owns its clip-set path/reference. Once a decoded
driver publishes one or more usable clip package paths, those paths are the
complete search domain for that driver. A shared role package such as
`bass_main.milo_ps2` is a compatibility fallback only when the decoded driver
publishes no path; it must not be appended to a character-owned package.
Mixing the two provenances allowed a stock GH2 `bassist_intro` to win name
selection over the converted GH1 graph even though GH1 uses `bassist_idle`
during that opening interval. The resulting arm and instrument-target pose
made the bassist visibly release the bass.

The runtime now derives one ordered, deduplicated search list through
`native_driver_clip_search_paths`. Its `driver_authoritative` result also
guards the later idle/intro fallback calls, applying the same rule to every
role without character, clip, instrument, or venue exceptions. Untouched GH2
characters still resolve their normal `alterna1_main` and `bass_main`
packages. Raw GH1 and converted-native `bassist_idle@f60` agree visually and
both leave the hands down; raw/converted `bassist_active_medium@f60` agree in
placing both hands on the authored bass. A current input-free native gameplay
run at song time 6 consumes the authored `[play]` event, selects
`bassist_active_medium`, and visibly puts both hands on the bass. Matched
frame-60 raw/native captures also cover all five packed families: Idle, Active
Medium, Active Fast, Win, and Lose. Active clips use both playing contacts;
the released contacts in Idle/Win/Lose match the GH1 source. Evidence and
current visuals are recorded under
`proofs/gh1-native-conversion-parity/instrument-hand-parity/`.

### GH1-to-GH2 upper-twist hierarchy and PS2 poll branches

GH1 `AnimServoUpperTwist` records twist1, twist2, and upperArm as three
siblings. That form is already native to GH2 PS2: stock band-role packages,
including `metal_drummer.milo_ps2`, use the same shared-clavicle hierarchy.
Stock selectable guitarist packages such as `metal1.milo_ps2` use the other
valid form, with twist2 parented beneath twist1. Conversion must preserve the
source form; changing a sibling graph into a serial graph composes the second
helper through the first helper a second time and deforms weighted arms.

Retail GH2 PS2 implements both forms in `SLUS_214.47`
`0x001823C8..0x00182958`. Object references resolve at `+0x24` for the live
upper-arm source, `+0x18` for twist2, and `+0x0C` for twist1. The comparison at
`0x00182454..0x00182460` selects the formula from `twist2.parent == twist1`.
The serial branch uses exact stored constants `-0.6660000086` and
`+0.3330000043`; after translation to native row convention, twist1 receives
the source-based `+0.666` correction and the authored twist2 child basis
receives `-0.333`. The sibling fallback starts at `0x00182670`, loads exact
`-0.5`, and composes the same source-based half-twist row for both sibling
outputs.

Fresh bounded disassembly of GH1
`SLUS_212.24:0x001878A8..0x00187C3C` proves that the source game uses the
same sibling formula. It loads exact `-0.5` at `0x001878FC`, exact `pi/2` at
`0x00187908..0x00187910`, and scales the extracted roll at `0x00187914`.
The earlier GH1 one-third/two-third upper-twist interpretation was wrong.

The runtime implements this pointer/topology branch directly. It does not
select by character, role, side, mesh, package, pose, or offset. The converter
promotes the GH1 transform meshes to GH2 `Trans9`, preserves all three parent
references, and rejects a graph whose twist1, twist2, and upperArm are not
siblings under one non-null parent. The packed sweep validates 26/26
upper-twist controllers across all 13 packages. This supersedes the rejected
2026-07-27 chain-reparent trial and the later RB3 world-space polling trial.
The later ihatecompvir routine documents a different engine generation; it is
not the GH2 PS2 `0x001823C8` contract.

Machine and visual evidence is under
`proofs/gh1-native-conversion-parity/instrument-hand-parity/`
`arm-twist-isolation/gh2-ps2-dual-branch-final/`. It includes the 13-package
conversion audit, object-level stock/converted transform inventories, exact
archive deployment verification, 55 passing character/gameplay regressions,
all eight GH1 guitarist views, all five GH1 band-role views, a logged GH1
sibling-branch sample, and a stock GH2 Metal1 serial-branch regression.

The stronger machine gate is under
`proofs/gh1-native-conversion-parity/matched-retail-twist-differential/`.
An exact-float runtime ledger and independent log-only implementation
recompute 6,000 controller events across all 13 GH1 performers: 4,800 upper
sibling events, zero serial substitutions, 1,200 fore events, and zero
failures at `5e-6`. Twist formula and branch selection are closed. Broader arm
presentation remains open for separate IK, attachment, animation, skinning,
instrument-binding, and visual acceptance.

### Pre-separate-color Mesh vertex slot

GH1 Mesh25 and GH2 Mesh28 both predate `MESH_REV_SEP_COLOR` (`0x25`).
Their four serialized floats after the normal are a shared payload whose
meaning is resolved only after the bone references load. The GH1/GH2 PS2
contract is:

1. Read all four values as raw floats.
2. If the resolved bone table is non-empty, preserve those floats as skin
   weights and clear retained vertex color to opaque white.
3. If the bone table is empty, convert the raw values through
   `Hmx::Color32` (multiply by 255, truncate, retain the low byte) and keep the
   result as vertex color.

The later RB3 in-memory `Color32` load documented by available source is not a
license to quantize the serialized skin weights in these PS2 packages. Packed
GH1 characters contain signed and over-one values that depend on raw-float
extrapolation and cancellation. Quantizing them before skinning creates the
long stretched polygons seen in the rejected 2026-07-27 checkpoint.

The original packed female-singer right-arm meshes make this distinction
decisive. Source Mesh25 and converted Mesh28 retain identical slot ranges:

```
fsing.18  R -0.00776636135..1          G -0.00824254565..0.711304605
fsing.19  R -0.17999959..1             G  0..1.22225523
fsing.20  R -0.0525420196..1.22225523  G -0.22225523..0.530012667
fsing.21  R -0.78294009..1             G  0..1.80243146
```

All four meshes have populated bone tables. The negative and over-one values
must therefore remain raw weights. The converter transfers them byte-for-byte,
and the runtime now preserves them until skin evaluation.

The converted Small Club object `main_room_stage.1.mesh` provides a packed
proof. It is an unskinned 105-vertex/94-face mesh using
`plaster_wall.mat`/`plaster.tex`. Raw Mesh25 and converted Mesh28 contain the
same serialized slot ranges:

```
R -0.259159088 .. 0.709698021
G -0.0831379443 .. 1
B -0.0317737013 .. 1
A  1 .. 1
```

Because this mesh has no bones, the corresponding retained Color32 ranges are
`R 0..0.745098054`, `G 0..1`, `B 0..1`, `A 1..1`. The conversion therefore
did not lose or reconstruct the authored baked color. The native renderer was
discarding it by assigning the slot to weights and forcing every vertex
white, producing the pale stone-like panels and trim seen in the 2026-07-27
review.

The runtime decoder keeps the raw slot through reference resolution, preserves
it as weights for every skinned mesh, and applies Color32 conversion only to
unskinned geometry. There is no Small Club, character, object, material,
texture, or channel-value branch. Focused tests cover both paths, including
signed/over-one skinned values and the unskinned Color32 low-byte behavior.
The combined MILO-scene, character, and gameplay regression passes 55/55.
Fresh hidden captures cover all eight GH1 guitarists and all five GH1 band
roles without the rejected stretched-polygons regression while retaining the
topology-driven sibling/serial upper-twist correction. Packed inspections,
runtime material logs, before/after frames, test logs, and deployed binary
identity are recorded in
`proofs/gh1-native-conversion-parity/small-club-vertex-color-contract/` and
`proofs/gh1-native-conversion-parity/instrument-hand-parity/`
`arm-twist-isolation/gh2-ps2-dual-branch-final/`.

## GH1 Arena/CharSys placement-record contract

Retail GH1 uses one Arena owner at global `0x00363748`; there is no separate
Arena-to-CharSys placement-vector transfer. Arena initialization
`0x001682F0` populates `walk_spot` at owner `+0xF0` and `stage_spot` at
owner `+0xE0`. CharSys `0x00281D00` consumes those same vectors from the same
owner.

Each record is exactly `0x40` bytes: one complete four-row transform matrix.
Both walk (`0x00281D80..0x00281DA0`) and stage
(`0x00281E6C..0x00281E8C`) paths copy four 16-byte quadwords at offsets
`0x00/0x10/0x20/0x30` before applying the transform. There is no unknown
field or metadata tail.

The complete packed corpus has 40 numbered helpers across all seven venues:
21 stage and 19 walk rows. All source Mesh25 and converted Mesh28 resolved
local/world transform digests match exactly. No helper has an external parent;
generic directory graph conversion independently covers parent composition.
Read-only live GH1 anchors, bounded disassembly, raw EE opcodes, and the
corpus ledger are in
`proofs/gh1-native-conversion-parity/matched-retail-small-club-anchor/`.

### Packed-to-live formation differential

The converter now exposes the source-derived placement mapping as first-class
audit data rather than requiring visual inference. For every venue and role,
`conversion-audit.tsv.venue-placements.tsv` records the source venue, target
venue, role, source helper, target Waypoint, flags, all 12 raw source-world
floats, all 12 emitted target-transform floats, and status. The full packed
sweep contains 28/28 passing rows: seven venues times guitarist, singer,
bassist, and drummer.

The target keeps the source translation bit-for-bit and normalizes each of the
three authored basis rows. Focused conversion tests independently verify that
rule, the four role/helper assignments, Waypoint flags, serialized target
values, and deterministic repeat output.

The main runtime's opt-in `[world-start-ledger]` row reports the selected
source, Waypoint, three flag masks, and all 12 live transform values at
round-trip-safe float precision. A hidden strict-native all-GH1 sweep joins
those values back to the packed audit:

- 28/28 live transforms have zero float-bit mismatches;
- all 28 select `type-script` and the source-derived role Waypoint;
- all 42 pairwise role translations are distinct; and
- all seven venues reach `state=playing` without compatibility loaders.

This closes the former overlapping-performer start-formation defect without
an asset, venue, character, or positional exception. It does not close matched
retail animation, pose, prop, lighting, camera, or full venue parity. Evidence
and reproduction scripts are in
`proofs/gh1-native-conversion-parity/formation-transform-contract/`.

## Conversion-blocking gap register

1. **G1 — Clean GH2 provenance — closed.** Redump-identified GH2 USA
   `SLUS-21447` is MD5
   `317968cd573b183f3c697bb94e8c8ee6`; extracted `MAIN.HDR` and `MAIN_0.ARK`
   hashes are recorded above. The pre-overlay header and immutable original ARK
   byte range preserve that corpus after deployment.
2. **G2 — ARK writer — closed.** Header, strings, paths, file entries, declared
   part size, physical append placement, and entry reuse are deterministic for
   the retail one-part PS2 archive. Loose and overlay output consume identical
   manifests; the second overlay appends zero bytes.
3. **G3 — Directory framing — closed.** GH1 revision-10 and GH2 revision-24
   roots/children, allocation metadata, body boundaries, external resources,
   recursive package contents, and cross-directory references reparse with
   complete residual accounting.
4. **G4 — Type/revision inventory — closed.** The complete packed GH1
   inventory is closed at 12,189/12,189 bodies across 21/21 observed rows,
   with exact inheritance order, byte consumption, semantic round trips, and
   source-to-target revision mappings. Mesh25-to-Mesh28, Tex8-to-Tex10, and
   Mat21-to-Mat27 are closed, including all 3,140 clean-target Mat27 bodies.
5. **G5 — Object writers — closed.** All 21 observed source rows and every
   required target body have revision-aware semantic serializers with
   edit/reparse coverage. Complete character, attachment, prop, animation, and
   venue packages are emitted rather than runtime object substitutions.
6. **G6 — Reference graph — closed.** Schema-typed fixups account for nulls,
   externals, proxies, owners, parents, targets, materials, textures, bones,
   controllers, groups, and recursive directories. Character and venue sweeps
   contain zero dangling references and no string-search fallback.
7. **G7 — Character contract — open for runtime parity.** The packed animation
   and package conversion contract has
   31 authored sets, 926 ACP clips, 25 ACG assets/29,808 nodes, all authored
   transitions, skeleton-channel resolution, and 39 deterministic native
   revision-24 clip-set packages. Thirteen model packages close skeleton,
   skinning/order, IK/controller serialization, faces, eyes, collide,
   cuff/sleeve, attachments, props, shadows, and events without
   identity-specific corrections. This structural accounting does not yet
   prove matched retail poses, animation, and presentation.
   Upper/forearm twist formula and branch selection are now numerically closed
   across 6,000 live events and all 13 performers; the remaining arm gate is
   IK, attachment, animation, skinning, instrument binding, and matched visual
   presentation. The 2026-07-27 smoke-test's overlapping start formation is
   now source-closed across all 28 venue/role rows with zero live transform-bit
   mismatches and zero pairwise role overlaps; that placement result is not a
   claim of complete character parity.
8. **G8 — Venue contract — open for runtime parity.** All seven venues convert
   geometry,
   Groups/Views, transforms, cameras/campaths, lighting/environments, material
   animation, particles, crowds, props, scripts/events, and draw state. The
   native sweep has zero unresolved or unsupported rows, but it only proves
   that each converted world loads. Matched retail placement, rendering,
   animation, lighting, script, crowd, and camera output remains required.
9. **G9 — ACP/ACG/ACS — closed.** ACP and ACG have lossless readers/writers;
   revision-18 ACP framing, its nested revision-5 SampleSet contract, ACG
   version-1 framing/transition semantics, and recursive ACS/DTB macro
   expansion are closed. All 926 packed ACPs convert deterministically to
   native revision-10 GH2 clips with exact set, flag, timeline, allocation,
   transition, and export-marker semantics.
10. **G10 — Texture/material packing — closed.** Same-platform HMX bitmap
    payloads, PS2 palette/swizzle bytes, straight-alpha semantics, Mat27
    `nextPass` lowering, animation routing, and all texture/material references
    are retained deterministically.
11. **G11 — Native conversion sweep — closed.** Two clean post-WorldDir11
    conversions match across all 118 files and the sorted hash ledger. The
    refreshed 54-row character and 62-row venue manifests are deployed; the
    second overlay reuses every row with zero appended bytes, ARK v3 framing
    remains exact, and all seven converted main roots reparse from the deployed
    archive as WorldDir11 with their authored preview-camera and section links.
12. **G12 — Runtime proof — open.** GH2 remains authoritative for gameplay,
    highway, HUD, and UI while GH1 characters and venues are independently
    selected. The existing video and seven-venue matrix are smoke tests only:
    they demonstrate loading and execution, not parity. Completion requires
    matched PCSX2/runtime comparisons across the complete character and venue
    matrix with native placement and GH1 compatibility paths disabled.

## Completion gates

The structural format gates are closed, but G7, G8, and G12 are reopened for
runtime parity. The corresponding machine-readable gates are:

1. Full GH1 and clean GH2 packed inventories with source hashes.
2. Byte-exact no-edit round trips for every supported source file.
3. Semantic edit/reparse tests for every field and reference category.
4. Zero unclassified object revisions and zero unexplained residual bytes.
5. Zero dangling, name-fallback, or asset-specific fixups.
6. Deterministic rebuild hashes from two complete clean-target passes.
7. Complete GH1 character and venue conversion sweeps.
8. GH2-native load with GH1 compatibility decoders disabled.
9. Mixed-content gameplay and visual/video proof.
10. Deployed assets and a linked final proof manifest.

Gates 1 through 6 are recorded by the current converter audit. Gates 7 through
10 remain open. The existing
`proofs/gh1-native-conversion-final/FINAL_PROOF_MANIFEST.md` is retained as a
smoke-test checkpoint and must not be treated as final parity proof.

## Runtime presentation staging and prop material state (2026-07-28)

World construction was previously performed by the first `Gameplay::draw`
call. That made a 30–74 second asset/decode phase look like gameplay frame
time and contaminated short visual proofs. `Gameplay::prepare_world` now
constructs the selected venue, performers, cameras, lighting, prop renderers,
and highway while the caller remains in its loading phase; the HUD is prepared
before the simple application enters `Playing`. Rendering still consumes the
same decoded objects and no asset selection changes.

The shared 60 Hz wait now budgets window-message pumping and uses a precise
deadline instead of a scheduler-quantum-dependent `sleep_for`. A native-only
GH2 Arena run measures 59.62 steady FPS, while a native-only converted GH1
Small Club run with converted `metal`, `female_singer`, `metal_bass`, and
`metal_drummer` measures 59.36 steady FPS. All measured draw windows report
zero initialization time after entry to gameplay.

Attached instrument props also use decoded `RndMat` state. The packed Flying V
string material is `kBlendSrcAlpha`, `alphaCut=false`, `cull=true`,
`zMode=kNormal`; its texture contains 30 zero-alpha and 40 opaque submitted
vertices. The renderer now applies blend, alpha-cut/threshold, cull, z-mode,
wrap, and material color to every attached prop mesh. The former literal
`guitar_strings.mesh` transparency branch is absent.

## Structural character surface state (2026-07-28)

Character surface behavior no longer depends on mesh or material spelling.
The former eye-name lighting exception and the obsolete hair-name attachment,
two-sided, anatomical, and local-space classifiers were removed. The renderer
now identifies an eye-controlled mesh only by resolving the decoded
`CharEyes.lookats` object links to each exact `CharLookAt.source` reference.
That structural set is used only by eye-controller diagnostics and the
optional hide-eyes debug view; it does not override material lighting.

`RndMat.use_environ` is the sole scene-lighting gate for character surfaces.
`RndMat.cull`, blend, alpha-cut/threshold, z-mode, texture wrap, and material
color likewise remain authoritative. Input-free stock GH2 Metal1 and converted
GH1 Metal checks retain visible eyes with decoded material state; the raw GH1
female-singer negative case has no `CharEyes` graph and therefore acquires no
inferred eye classification. Evidence:
`proofs/gh1-native-conversion-parity/structural-character-render-state/`.

## Native prelit/environment lighting rule

GH2 PS2 `Ps2Mat::Select` at `0x0019CFE0` reads `RndMat.use_environ` from
`+0x40` and `RndMat.prelit` from `+0x9C`. The branch at
`0x0019D12C..0x0019D154` enables one material light channel exactly when
`use_environ || !prelit`. Therefore only `prelit && !use_environ` bypasses
fixed lighting. A prelit material which opts into an environment retains its
baked vertex colors as material input while consuming decoded environment
ambient, approximate lights, dynamic lights, and animation.

The runtime implements that two-field truth table directly. It contains no
source-family, venue, object, mesh, material, color, or brightness classifier.
A live converted GH1 Basement run restores baked venue/performer detail while
retaining animated blue light, and the native GH2 Arena positive regression
retains authored cyan/green lighting. Evidence:
`proofs/gh1-native-conversion-parity/material-light-channel-contract/`.

## Decoded material visibility instead of material identity (2026-07-28)

Venue geometry visibility no longer depends on the literal material names
`invisible.mat` or `ray_blocker.mat`. A complete seven-GH1/eight-GH2 main-venue
sweep found that Arena and Festival clip-mask materials carry decoded material
alpha zero. Theatre's same-named material instead has alpha one and no mesh
users, while its 16x16 `invisible.tex` has zero alpha in all 256 pixels and is
also referenced by a differently named material. No swept main venue contains
`ray_blocker.mat`.

The ordinary renderer therefore consumes decoded material color/alpha,
texture alpha, blend, alpha-test, culling, and depth state without a
material-name suppression rule. Debug reachability likewise treats decoded
zero material alpha as non-pickable rather than testing a name. Hidden,
input-free Arena, Festival, and Theatre guards each reached playing state and
produced fixed-time frames 2 and 120 with no exposed clip masks or floor-gap
regression. Evidence:
`proofs/gh1-native-conversion-parity/render-material-identity-contract/`.

## GH2 WorldFx directory and live-state contract (2026-07-28)

The current stock GH2 sweep covers 24 base performer packages and 13 crowd
packages. Seventeen performer packages contain 87 `WorldFx` rows and all 13
crowd packages contain one, for 100 decoded rows with zero failures. Every
observed row is revision 1 followed by a complete revision-8 `RndDir` body.
The shared object reader therefore parses the inherited
`ObjectDir`, `RndTransformable`, `RndDrawable`, and `RndAnimatable` state rather
than scanning offsets. It preserves the proxy path, subdirectories,
environment, test event, local/world transform, parent, constraint, target,
preserve-scale state, serialized visibility, draw order, frame, and animation
rate. Parse/serialize round trips reject residual bytes.

Retail `SLUS_214.47` static analysis separates serialized directory state from
the effect's live running state. `WorldFx::Start` at `0x002726B8` writes one to
offset `0x98`; `WorldFx::Stop` at `0x002728E0` clears the same word; and
`WorldFx::Poll` at `0x002725D0` exits unless that word is set. The handler at
`0x00272BC8` dispatches the exact `start` and `stop` symbols. Consequently,
`RndDrawable.showing` is not used as an initial running-state shortcut.

The common character-effect runtime resolves each decoded proxy relative to
the owning character MILO and archive, merges its authored visual
subdirectories, loads its textures and `AnimFilter` rows, attaches its
`local * parentBoneWorld * characterWorld` transform, and respects decoded
draw order. The same implementation serves stock crowd and performer effects.
Performer transitions are emitted by the decoded `char_objects.dtb` type
script rather than by effect names. In stock Glam1, `solo_on` followed by
`peak_on` starts `hand_flames_L` and `hand_flames_R`; `peak_off` stops them.

The focused decode/type-script tests pass. A hidden, input-free gameplay run
loads stock Glam1, resolves all five authored WorldFx proxies, logs both hand
flames active at song time 108-109 seconds, reaches the normal playing state,
and visibly renders the effects on both hands. Evidence:
`proofs/gh1-native-conversion-parity/worldfx-performer-contract/`.

## Stock character opaque-row closure (2026-07-28)

The runtime character graph now consumes the exact `OutfitLoader1` and
`CharWalk1` readers already used by the offline MILO round-trip layer.
`OutfitLoader1` follows the GH2 retail save/load contract at
`SLUS_214.47:0x0018AEC8` and `0x0018B170`: revision/Object fields, loader
directory, `u16` category count, selected/shown bytes, `u16` outfit counts,
and hide/desire/exclude bytes. `CharWalk1` is revision 1 plus a complete
`ObjectFields0` row and has no additional serialized payload.

This is a format promotion, not a fabricated runtime controller. The missing
`CharWalk::Poll` motion body and OutfitLoader live switching logic remain
explicitly fenced. The current 24-package performer sweep decodes all 20
OutfitLoader, 19 CharWalk, and 87 WorldFx rows with zero failures and zero
opaque rows. The explicit 13-package crowd sweep adds 13 WorldFx rows and also
has zero failures and zero opaque rows. Evidence:
`proofs/gh1-native-conversion-parity/stock-character-opaque-row-closure/`.

## GH2 character start/waypoint runtime contract (2026-07-28)

The serialized `CharWalk1` closure above is distinct from character start
placement. ihatecompvir `Waypoint.cpp` proves that `waypoint_find` returns the
first registered waypoint whose flags overlap the requested mask. GH2 retail
registers `waypoint_find` and `waypoint_last` at `SLUS_214.47:0x00190660`;
their command bodies are `0x00191020` and `0x00191160`. The latter scans the
global registry and moves the selected node to its end. Stock
`char_objects.dtb` then implements `start_at` as `Character::teleport` followed
by `waypoint_last`.

The native type-script host now mirrors that contract with a shared,
construction-order registry. It identifies a character by its live world
object name rather than its asset package root, so stock guitarist logic
correctly resolves `guitarist0` and the single-player flag `1`. Gameplay
consumes the selected decoded source-waypoint index; no character, venue, or
waypoint name chooses the transform. A hidden current-build run routes
guitarist, singer, bassist, and drummer starts through the type script.
Evidence:
`proofs/gh1-native-conversion-parity/waypoint-type-script-contract/`.

This closes start selection/teleport routing only. The later live CharWalk
section records the separately recovered nearest/path contract.

## GH2 live CharWalk controller contract (2026-07-28)

The outer retail controller is no longer wholly unknown. Static analysis of
`SLUS_214.47` recovers `CharWalk::Poll` at
`0x00184B10..0x00184CC8`, its start/setup body at
`0x00184CC8..0x00184FD0`, prediction/regulation bodies through
`0x0018592C`, and the walk/turn/stop selection and plan bodies through
`0x00187278`. The decoded field accesses match ihatecompvir's `0xCB0` RB2
layout, including `mState` at `0x50`, `mCurNode` at `0x54`,
`mCurWaypoint` at `0x58`, 12-byte `WalkClip` rows at `0x5C`, 12-byte
`PlanPoint` rows at `0x70`, the owning `Character*` at `0x08`, and the
selected walk clip's half-range beat remainder at `0xC78`.

Retail has exactly `kStateNone=0`, `kStateGoing=1`, and
`kStateStopping=2`. `Poll` advances the ordered `WalkClip` list while going,
changes to stopping when that list is exhausted, validates the final clip
against the owning character's most-playing driver, and calls one shared
regulation body in both active states. The helpers query authored
`walk_walk`, `walk_turn`, and `walk_stop` clip groups, predict clip movement,
fill `PlanPoint` rows, and normalize the current waypoint direction.

The native bridge now keeps this retail state separate from its
turn/walk/stop playback phase: turn and walk publish `Going`, stop publishes
`Stopping`, and completion publishes `None`. This corrects camera/controller
state without selecting an asset by name. The remaining fenced boundary is
the fully typed path-regulation math and live `CharClipDriver` ownership, not
the state enum or outer state machine. Evidence:
`proofs/gh1-native-conversion-parity/charwalk-runtime-contract/`.

The target-side configuration and clip-family boundary is now live as well.
The type compiler retains data-only type members and reads the stock
`BandCharacter/guitarist` `walk_delays`, `walkspot`, and `max_walk_wait` rows
plus `CharWalk/guitarist.path_radius`. The resulting exact values are delay
levels `{false,false,35..55,20..40,false}`, waypoint mask
`kWalkSpot|kSoloWalkSpot` (`0xC0`), maximum wait `6`, and path radius `12`.
Native packages then resolve `walk_turn`, `walk_walk`, and `walk_stop` from
their serialized `CharClipGroup` rows and load every referenced clip.

All eight converted guitarist-main packages pass a 623-clip sweep with zero
remaining GH1 membership bits and exactly one of each required walk group.
An extraction from the deployed ARK confirms Metal turn/stop flags
`0x00C00802` and walk flags `0x00C02802`. Hidden, input-free,
`--require-native-assets` runs confirm the typed configuration and
20-turn/16-walk/16-stop groups in both converted GH1 Small Club and untouched
GH2 Arena. Evidence:
`proofs/gh1-native-conversion-parity/native-charwalk-controller/`.

The normal-walk scheduler is now recovered separately from the movement
regulator. `BandCharacter::Poll` at `0x0010AD00..0x0010AD64` starts a request
when the current sampled delay becomes negative. `0x0010C3D0` stores the
sample epoch, samples only enabled two-number rows with `RandomFloat`, and
stores `1e30` for disabled rows. `0x0010C2E0` creates a task spanning
`[now, now + max_walk_wait]`; task completion or expiry resamples the five
rows. The normal request mask is exactly `kWalkSpot` (`0x40`), while the type
property remains the broader `kWalkSpot|kSoloWalkSpot` mask.

The task at `0x0011FC88..0x0011FF94` calls retail
`Waypoint::FindNearest` (`0x00190770`) with `kWalkSpot`. It scans the mutable
global registry in order, accepts any overlapping flag bit, and chooses the
minimum squared world-position distance. It then calls the path finder at
`0x00190E98` with destination mask `requestFlags` and blocked mask
`requestFlags|kWalkSpot`.

The path finder is an authored-connection graph operation, not a cyclic or
last-named waypoint shortcut. It scans candidate destinations in the current
global registry order. For each non-source destination sharing any requested
bit, `0x00190D30` performs source-order depth-first search over the decoded
`Waypoint::mConnections` rows. It marks visited nodes per attempt, rejects an
intermediate node sharing any blocked bit, permits the destination despite
that mask, and appends the successful route without the source node. The
selected destination's registry node is then spliced to the list tail, which
rotates later requests fairly. Native `source_charwalk_find_route` implements
this exact generic contract and the live native path consumes it.

GH2 Waypoint revision 3 is now decoded in exact load order:
`ObjectFields0`, legacy `RndDrawable3`, `RndTransformable9`, flags, the
counted connection ObjPtr vector, radius, Y radius, and angular radius.
Runtime retains every connection name and the three radii with zero residual
bytes. Arena proves the resulting graph directly:
`walk_left -> walk_center -> walk_right`, while `walk_solo` bridges
`walk_center` and `walk_stage`. The source positions and route rows are fed
to CharWalk; no venue or waypoint name appears in route selection.

The request's local-direction test is the strict
`abs(y) < abs(2*x)` lateral test. Lateral requests retain
left/right as the turn hint and choose forward movement 25 percent of the
time; forward requests have no turn hint; backward requests carry backward
turn/final hints. Walk flags add normal (`0x1000`) below excitement 3 or
extreme (`0x2000`) otherwise. Native now implements these facts only on the
GH2-native path; the raw GH1 compatibility selector remains isolated.

The request's walk clip is not an untyped best-fit search.
`CharClipGroup::GetClip(int)` at `0x00169E40..0x0016A0F8` scans cyclically
from serialized `mWhich + 1`, requires every requested walk/style flag,
promotes the accepted entry into the next cyclic slot, and updates `mWhich`.
Native now preserves that group state, performs the same promotion, and keeps
the selected walk clip for the request instead of selecting a different loop
clip after every cycle.

The runtime clip reader also now retains the complete serialized transition
graph—target clip plus ordered `(currentBeat,nextBeat)` nodes—along with
`startBeat`, `endBeat`, and `beatsPerSecond`. The converter already emitted
these target rows from the GH1 ACG; discarding them after native load would
make retail turn/walk fitting impossible.

GH2 retail `CharClip::FindNode` is now recovered at
`0x00196888..0x00196A70`, with its jump table at `0x00417810`.
Mode 2 returns null. Mode 3 first searches the target transition for the first
node whose current beat is at or after the requested beat; mode 4 performs the
corresponding reverse scan. When a permitted search has no authored row, the
function synthesizes a node from the current/target clip timing. Mode 4 clamps
the current beat up to `current.endBeat - target.range/2`; the target beat
starts at `target.startBeat`; and the target clip's `playFlags & 0xF000`
beat-align nibble advances that beat to the first same-phase value not before
its start. Native
`source_char_clip_find_transition_node` implements this generic contract with
focused authored-edge, null, mode-3 fallback, and mode-4 fallback tests.

The retail turn/root-fit scorer is now recovered and implemented as a generic
clip/transition operation. It asks the owning driver for `FirstPlaying`
(skipping a zero-weight stack head), searches current-to-turn with
`FindNode(...,3)`, searches turn-to-walk with `FindLastNode`, and predicts the
root and `bone_facing` position/rotation through both spans. A candidate is
rejected when its predicted remaining squared distance is greater than the
unturned baseline. Among the survivors, the scorer chooses the strict minimum
wrapped absolute heading error; equal scores therefore retain authored group
order. Walk heading is sampled over one authored beat, including the
`walk.range/2` correction. Playback uses the resulting authored beats: enter
the turn at the first transition's target beat, leave it at the second
transition's current beat, and enter the walk at the second transition's
target beat. Focused tests cover facing interpolation, `FirstPlaying`,
distance rejection, angular selection, and authored transition timing.

The geometric corridor half of `RegulateWalk` is also typed. It advances the
current segment only after the forward prediction crosses the current
waypoint plane, projects the backward prediction onto the authored segment,
confines lateral excess to `path_radius`, and computes the signed correction
as `-asin(crossZ/(max(5,lenA)*max(5,lenB))) * Character[0x23C]`. The
`0x00161918` writer fills `Character+0x238/+0x23C/+0x240` from the three
TaskMgr time deltas, and driver update code independently consumes
`+0x238/+0x23C`; `+0x23C` is therefore the active frame delta, not the
`CharServoBone::mMoveSelf` boolean. The signed cross product is important; an
unsigned `acos` interpretation is incorrect.
Focused tests cover both turn directions, radius confinement, segment
crossing, registry rotation, and blocked intermediate nodes.

The clip plan and stop chooser are now recovered and implemented generically.
`0x00186F80` starts with the turn/walk schedule, follows the walk clip's
authored self-transition, predicts one-beat `bone_facing` distances, and emits
12-byte `{clipIndex,beat,cumulativeDistance}` plan rows through the first
overshoot. `0x00186350` reverse-predicts every source-order stop candidate
whose flags contain the request, preserves the removed overshoot row as an
alignment sentinel, matches integer beat parity, and searches plan rows in
two-beat steps. Its strict score is
`max(radiusError-3,0)*5 + abs(angleError)*57.295776`; ties retain authored
order. The winner replaces the last path point with its predicted stop-start,
clamps the chosen longitudinal adjustment to the decoded waypoint radius,
adjusts the final destination by the same amount, trims the walk schedule, and
appends `{stop,startBeat,previousWalkEndBeat}`.

`ForwardPredict` and `BackPredict` now consume that schedule directly.
Forward prediction carries overrun across each
`previousEnd+beatRemainder -> nextStart+beatRemainder` boundary. Backward
prediction returns the current authored waypoint on intermediate segments and
reverse-predicts from the adjusted final target on the last segment. Native
playback advances the resulting turn/walk-repeat/stop rows at their authored
transition beats and feeds both predictions into the corridor controller.

The remaining positional half of `RegulateWalk` is recovered from
`0x001856F8..0x001858F4`. GH2 stores `mCurPoint` at `+0x6C`,
`mLastPoint` at `+0x70`, the 12-byte PlanPoint array at `+0x74`,
`mOffsetSpeed` at `+0xC74`, and the beat remainder at `+0xC78`. When the
current clip/beat passes a PlanPoint, retail compares the remaining authored
PlanPoint distance with the real distance from the character through the
remaining waypoint path, divides that error by
`min(mLastPoint-mCurPoint,2)`, advances `mCurPoint`, and moves the character
toward the forward prediction by
`frameDelta*mOffsetSpeed/length(delta)`. The stop chooser's
`0x00186934` write also proves that the selected stop-alignment point replaces
the provisional overshoot count in `mLastPoint`. Native implements both
details generically with focused schedule, point-advance, speed, and position
tests.

This closes the recovered regulator math. The remaining boundary is complete
outer `CharDriver::Poll` scheduler ownership, deployment across the full
character matrix, and matched retail comparison. A hidden, input-free,
native-only Arena run now proves the implemented path through
`walk_right.way -> walk_center.way -> walk_left.way`: seven schedule rows,
twenty PlanPoints, selected point 17, authored turn, four repeated walk
phases, authored stop, and destination completion. The linked silent video
uses the source-authored `flr_far_lft01` camera:
`proofs/gh1-native-conversion-parity/charwalk-motion-plan-complete/`.

The adjacent camera-facing predicate is also direct retail evidence. GH2
registers `actually_walking` at `0x0010CD64`; dispatch at `0x0010CD88` loads
the `CharWalk*` from `Character+0x248` and calls `0x00184FD0`. That method
returns true only when `mState > 0`, a most-playing driver exists, and its clip
matches `mClips[min(mCurNode-2,last)]`. Native now checks its recovered active
state and concrete current walk clip rather than citing the older GH1
predicate or equating only one playback phase with walking.

## GH2 PS2 CharClipDriver core

The live node core is now recovered from `SLUS_214.47`, not inferred from
captured poses. Constructor, copy/destruction, Evaluate, ScaleAdd, alignment,
and event-cursor code occupy `0x00198660..0x00199084`. The 0x3C-byte node
layout is fully accounted through `nextEventIndex` at `+0x34`; the copied
`+0x38` word remains explicitly opaque because none of those recovered
routines initializes or reads it.

Native playback now preserves the retail constructor distinctions that the
older viewer approximation erased. Default/sentinel starts strip inherited
zero-fraction heads, mode 2 deletes the old stack, transition modes consume
authored nodes, and a missing transition falls back to the clip start at full
weight. Explicit starts retain their supplied beat and ramp at zero weight.
Mode 8 writes the retail `0.000001` epsilon. A zero blend width is not an
immediate constructor cut: it remains a stack node and reaches one during its
first active Evaluate. Explicit negative overrides are preserved rather than
host-clamped.

Positive clip `range` now uses the recovered PS2 global random path. Static
code at `0x002D9B10..0x002D9DC8` proves the fixed seed 666, 256-word seed
table, initial cursors 0/103, PS2 249-word cursor modulus, low-16-bit
`1/65536` float conversion, and `RandomFloat(0, range)` call. The sampled
offset is wrapped over half the authored clip beat span exactly as the
constructor does. This PS2 state is deliberately separate from the recovered
GH2 XEX random state, whose cursor modulus is 250.

Evaluate receives absolute task beat, task-beat delta, and real-time delta
separately. Native implements next-first arbitrary-depth stacks, outgoing
`advanceBeat` ramping, normal/real/user time modes, nearest-half-period
alignment, strict beat-event crossings, loop overrun, cosine-eased residual
weights, full-stack retirement, `FirstPlaying`, and `MostPlaying`. Crossed
beat-event rows are retained with clip, index, beat, and symbol for their
owning scheduler; they are not sent to a guessed script owner. Focused driver,
source-truth, and left-hand tests pass. Fresh hidden strict-native GH1 Small
Club and GH2 Arena runs each reach gameplay with four GH2-layout performers
at steady 59.961 and 60.043 FPS. Evidence:
`proofs/gh1-native-conversion-parity/charclipdriver-exact-core/`.

## GH2 PS2 CharDriver outer Poll

The owning PS2 scheduler is now statically recovered at
`0x00171830..0x00171C64`, with its exact stack-starved predicate at
`0x001710B8..0x001710DC`. Poll reads the head at `CharDriver+0x38`, the
optional starved-event symbol at `+0x3C`, the saved `DataNode` at `+0x40`,
the previous beat at `+0x48`, `realign` at `+0x4C`, and beat scale at
`+0x50`. Constructor evidence proves `+0x3C` starts as the engine's empty
symbol; it is not an event-handler object.

Native now applies the retail scheduler order: beat realignment, optional
starved-event callback, graph-loop replay, saved-node re-resolution for
node-loop mode, then node Evaluate/ScaleAdd. The PS2 predicate considers a
null head or a head without `next` starved. Node-loop replay requires an
owner-provided saved-node resolver; an unresolved node no longer fabricates a
replay of the current clip.

`CharClipDriver+0x2C` is the clip-reference owner passed by `CharDriver`.
Enter, exit, and strict counted beat events now cross a synchronous
owner-callback boundary in retail order while the lossless
clip/index/beat/event record remains available for audits. Binding that
callback to a live script is deliberately deferred when the owning script
object has not been decoded; native does not guess a type-script recipient.

The final serialized byte of `CharDriver3` is now correctly named `realign`.
A 13-package converted-GH1 sweep accounts for 30 driver rows: only
`metal_drummer/main.drv` and `metal_keyboard/main.drv` set it, and the other
28 rows clear it. Gameplay derives main/right/left Poll settings directly
from the decoded driver objects.

Focused driver, source-truth, and left-hand tests pass. Fresh hidden,
input-free strict-native GH1 Small Club and GH2 Arena runs reach
`state=playing` with four GH2-layout performers at 60.017 and 59.935 steady
FPS. The full field sweep, logs, static ranges, and deployed-binary hash are
recorded in
`proofs/gh1-native-conversion-parity/charclipdriver-outer-poll/`.

The live saved-`DataNode` and script-owner boundary is now implemented.
Every decoded `CharDriver` is registered under its serialized object name,
including secondary drivers such as `wings.drv`; pre-bind messages queue in
source order, while live queries execute once and are never replayed. Exact
clip/group resolution, source-order group state, saved-node node-loop replay,
starved callbacks, beat scale, realign, and clip enter/exit/beat events all
route through the decoded character type instance.

The final GH2-only message gap, `play_if_safe`, is recovered from the PS2
wrapper at `0x00172D58..0x00172E0C` and core at
`0x001714D0..0x001716F8`. The retail predicate subtracts the current
first-playing node's remaining beat span from the requested event window. A
matching clip is safe when `(clip.flags & safeFlags) == 0`, or when its
authored beat duration is strictly smaller than that adjusted window. Native
implements and focused-tests those exact branches.

Guitarist chart motion crosses one additional native class boundary rather
than a missing DTA handler. GH2 PS2 `BandCharacter::Handle` at
`0x0010C9E0..0x0010CB7C` maps `play`, `idle`, and `wail_on` to the authored
`normal`, `idle`, and `extreme` groups through the group helper at
`0x0010B7F8`; `gtr_solo_on` selects `solo`, while both off messages use
`0x0010C5B8` to restore `normal`. The live type owner now exposes those exact
class-level messages to decoded `main.drv` group playback. It does not insert
a guessed `play` handler into packed `char_objects.dtb`.

Stock BAND_COMMON also establishes one cross-format conversion requirement:
GH1 band assets share GH2's active (`0x4`), idle (`0x40`), lose (`0x80`), and
win (`0x100`) domains but have no distinct GH2 venue-intro clips. GH1's
drummer domain likewise has normal/all-beat/double/half motion but no GH2
`kBandNosnare` (`0x200`) member. The converter therefore promotes every GH1
band `kBandIdle` clip into target `kBandIntro` (`0x400`) and
`kBandIntroIdle` (`0x1000`). When the source clip set structurally contains
all three drummer-specific all-beat/double/half domains, its normal active
clips also gain `kBandNosnare`, allowing the same source-authored normal
motion to satisfy GH2's extra chart state. All source flags and samples are
retained. These are role/domain/semantic-bit rules, not asset-name fallbacks.
The full matched character-motion matrix remains open.

## GH1/GH2 hand-strum semantic bridge

The source and target games serialize the same guitarist hand behaviors under
different clip-name domains. GH1 `config/gen/charsys.dtb` assigns event IDs
24, 25, and 26 to `strum_down_long`, `strum_pluck_down`, and
`strum_pluck_short`. Its `hand_strum_mapping` then uses those clips as the
long, chord/pick, and short semantic samples across the default, punk, and
soft-pick mappings.

GH2 `config/gen/midi_parsers.dtb` addresses the corresponding
`StrumMap_Default` children as:

```text
strum_short_01 .. strum_short_04
strum_pick_01  .. strum_pick_02
strum_long_01  .. strum_long_04
```

Conversion therefore retains all four source `CharClipSamples` entries
(`strum_open` plus the three authored motion samples) and emits ten
target-family aliases:

```text
strum_pluck_short -> strum_short_01 .. strum_short_04
strum_pluck_down  -> strum_pick_01  .. strum_pick_02
strum_down_long   -> strum_long_01  .. strum_long_04
```

Each alias reuses the exact converted source body and allocation size. The
rule depends only on the packed semantic mapping, never on a character name.
A focused round-trip test requires all 14 entries, the complete regeneration
passes with zero blockers, and the deployed ARK replaces all eight native
strum packages before exact verification.

The live all-guitarist matrix runs 1,200 frames through real GH2 note events.
Every package resolves all ten target names, publishes its attached
guitar/string transform, emits post-controller hand samples, reaches
`state=playing`, reports no missing clips or invalid handlers, and holds
58.911-59.840 steady FPS. This proves the naming bridge and live controller
path. It does not by itself close matched retail visual hand-placement parity.
Evidence:
`proofs/gh1-native-conversion-parity/instrument-hand-parity/gameplay-attachment-matrix/`.

## GH1 character highway surfaces

GH1 authors each selectable guitarist's unique highway bitmap in
`config/gen/characters.dtb`:

```text
(<character> (track_surface "track/surfaces/<source>.bmp"))
```

The source asset is compiled at
`track/surfaces/gen/<source>.bmp_ps2`. GH2's runtime character contract
selects `track/surfaces/gen/<package>_keep.bmp_ps2`. The offline converter now
parses the GH1 DTB table, joins its character symbol to the already-compiled
character manifest, reads the exact compiled source bitmap, and emits it at
that GH2-native destination. This preserves non-identical authored mappings
such as `hair_metal -> hair.bmp` without a name exception.

The deterministic bundle contains eight `character-highway-surface` rows.
The deployed patch overlay replaces the two colliding GH2 paths and adds the
six absent paths, then passes exact ARK-v3 verification. A hidden,
input-free, strict-native Alterna run resolves and selects
`alterna_keep.bmp_ps2` from the deployed archive. The same run retains GH2
song, HUD, highway renderer, and UI ownership; only the character-authored
surface bitmap crosses the content boundary.

The live guitarist transition matrix independently runs all eight converted
GH1 guitarists for 600 frames. Each decoded `BandCharacter` owner reports zero
unhandled messages and routes the GH2 `PART GUITAR [play]` marker through the
recovered `normal` group to a source-authored clip at 59.248-59.788 steady
FPS. Evidence:
`proofs/gh1-native-conversion-parity/native-guitarist-play-matrix/`.

## Cross-family venue frame-pacing proof

The native conversion path now has a complete performance/isolation matrix:
all seven converted GH1 venues and all eight stock GH2 venues run hidden and
input-free with the strict native gate, one GH2 song, and four GH2-layout
performers. All 15 rows exit zero, reach gameplay, report no gameplay-time
initialization, and sustain 58.785--60.201 FPS.

The correction is systemic. D3D9 no longer adds a refresh wait before the
application's own accumulated 60 Hz deadline; immutable vertex/index buffers
are cached only when the decoded scene proves that a mesh has no skinning,
animation, override, dynamic-prelight, or fade dependency; authored hierarchy
lookups and base world matrices are cached; consecutive identical environment
state is reused; and disconnected XInput slots are probed less frequently
without reducing connected-device polling. No source or target asset name
selects any of these paths. The machine-readable matrix, bounded logs, and
reproducible runner are in
`proofs/gh1-native-conversion-parity/frame-pacing-full-matrix/`.

## Cross-character animation retarget proof

The runtime now separates a performer's rendered model from its animation
owner. The diagnostic interface accepts a role-scoped model override and a
role-scoped animation override, then resolves each package independently from
the archive. It loads the animation owner's `BandCharacter` program and clip
archives while retaining the target model's geometry, materials, skin, bind
skeleton, and morphs. This is a general runtime seam rather than a character
name replacement.

An input-free proof renders the converted GH1 female singer in the
`guitarist0` waypoint while Judy Nails (`alterna`) owns the guitarist program
and clips. Absolute clip locals are rebased from Judy's factual bind skeleton
onto the singer's factual bind skeleton by identical transform name. The
conversion preserves each source bind delta, starts from the target bind, and
reports matched outputs, channels, extrema, and non-finite values. It invents
no bone aliases. The proof matches 30 of Judy's 63 published outputs and
reports zero non-finite values.

The singer package has no guitar attachment row, so the runtime imports
Judy's authored `bone_pos_guitar.mesh` transform and parent chain as a
transform-only proxy. Attached-prop world resolution now includes those live
proxy locals, just as typed pose acquisition already does. This keeps the GH2
guitar attached and animated without a model-specific offset. Conversely,
the target model's actual parent graph identifies its mic/stand as descendants
of `bone_pos_mic`; those descendants are suppressed only because this instance
is assigned a non-singer role.

### Proportion-aware instrument/arm contract

The proof path no longer depends on the source performer's limb proportions.
After the imported prop hierarchy exists, the runtime installs the animation
owner's hand-controller graph only when the target supplies the exact factual
hand -> forearm -> upper-arm chain and the guitar supplies the exact authored
fret/strum targets. Target-authored controllers take priority. The imported
graph includes the applicable `CharIKHand`, `CharIKMidi`, standard hand
drivers, weight setters, and twist controllers; missing requirements reject
that controller instead of creating aliases or offsets.

For each accepted arm, the runtime measures source and target reach from bind
translations (`|hand.local.pos| + |forearm.local.pos|`). It groups those facts
by imported instrument root and scales only that proxy root's bind/local
translation by the mean target/source reach ratio. The guitar geometry, scale,
fret/strum targets, and target character skeleton are not resized. Thus the
instrument establishes the two hand constraints, the two-bone IK solver bends
each target-sized arm to those constraints, and the target's applicable twist
controllers run afterward. There are no model names, per-character constants,
or tuned offsets in this path.

The release proof covers two different target proportions during chart-driven
playing. The female singer measures 19.321 units of reach and applies a
0.91715 attachment ratio; the largest observed hand-target distance is 18.497.
The male singer measures 21.719 units and applies a 1.03099 ratio; the largest
observed distance is 18.540. Both remain within reach with solver and target
blend weights at 1, and both upper-arm twist pairs are active. The synthetic
contract independently verifies that a 16-unit target arm driven by a 20-unit
source arm moves an imported 12-unit attachment offset to 9.6 (ratio 0.8).

The fret hand also uses a geometry-derived contact correction when source and
target hand topology differs. The runtime selects the highest-detail authored
LOD, gathers vertices with nonzero skin weight for the exact hand bone, and
evaluates every gathered vertex with the renderer's exact bind path:
`vertex * serialized bone offset * bone bind world`, blended across the four
serialized weight slots. The resulting model-space point is transformed into
wrist-bind space before the weighted centroid is accumulated. This matters for
converted GH1 meshes whose serialized bone offsets do not equal the mesh
object transform. It moves
the target wrist so the target cluster centroid coincides with the source
cluster centroid after bind-frame orientation retargeting. This applies only
to the authored `bone_fret_hand` grip target: the strumming hand retains its
source wrist/pick target because a palm-centroid constraint is not the authored
contact semantic for that hand. Judy's left hand contributes 253 weighted
vertices across seven highest-detail meshes; the singer contributes 122
vertices in one rigid hand cluster. No model name or fitted offset is used.

This is release-quality for compatible humanoid rigs with exact named arm
chains. A target without the source's finger chains retains its own rigid hand
geometry; the runtime does not fabricate finger bones. A rig with an
incompatible rest topology (for example, a seated drummer bind used as a
standing guitarist) must provide a canonical rest profile or be rejected by
content qualification. Non-identical skeleton naming likewise requires an
explicit factual mapping in the planned external character configuration.

The input-free playing video, two target-body stills, and compact audit are in
`proofs/female-singer-as-judy-guitarist/`.

### Male singer driven by GH1 Clive Winston

The same general path now binds the converted GH1 male singer model
(`metal_singer`) to Clive Winston's converted GH1 animation owner (`classic`).
The source is resolved from the packed character facts rather than a display
name: `classic_main.milo_ps2`, `classic_strum.milo_ps2`, and
`classic_fret.milo_ps2` supply the main, strum, and fret programs. The runtime
loads 101 clips containing 6,516 frames and 361,094 channels, with 2,569 of
6,083 published output rows matching the singer's exact transform names.

The corrected bind-skin measurement resolves Clive's left-hand contact to
`(3.22821, 0.39944, 0.03615)` in wrist space from 234 weighted vertices across
nine highest-detail meshes. The singer's rigid hand cluster resolves to
`(2.18178, 0.28600, -0.89748)` from 43 vertices in one mesh. The imported
guitar root uses the derived 0.98797 arm-reach ratio. Across 480 logged arm
solver events, the largest hand-target distance is 21.286726 against the male
singer's 21.719192-unit arm reach, with both solver and target blend weights
active. Chart events drive five strum selections, five hand-map selections,
and 56 fret-position changes during the captured run.

The five-second, input-free fixed-step active-playing proof and its compact
machine audit are in `proofs/male-singer-as-clive-gh1-guitarist/`. The user
accepted this male-singer/Clive retarget proof on 2026-08-08.
