# Singer finger-rig staging

## Shipping pipeline (2026-09-05)

The singers now use the automated **native MILO** finger graft and skin-weight
transfer, not the manual Blender staging workflow below. It generates both
GH2 defaults and GH1 alternates in `DLC/core.singers`, preserving source body
proportions and NPC assets. See [the pipeline and proof report](../../docs/PLAYABLE_SINGER_FINGERS.md).

`milo_rig_export` now preserves the source's **signed floating-point skin
weights**. These fields must not pass through Color32/unsigned byte conversion.

## Legacy manual Blender workflow

This pipeline prepares a singer model for manual finger weight painting without
changing the singer's proportions. It uses a guitarist only as a factual donor
for the left/right finger hierarchy and bind orientations.

The local `milo_rig_export` helper reads GH2 revision-28 bone palettes and
PostLoad weight channels directly from the loose MILO. Grim supplies the
source-visible geometry, hierarchy, textures, and materials. Blender combines
those two factual views into a native armature; this avoids treating Grim's
static GH2 glTF export as if it already contained a skin.

Native transform nodes are not assumed to be in the active View/LOD mesh's
bind space. The importer reconstructs each bind matrix from the serialized
per-mesh bone offset (`mesh world * inverse(offset)`), resolves any accessory
meshes authored in alternate local spaces by the dominant internally
consistent bind cluster, and derives the source coordinate-system mapping
against the corresponding weighted geometry. It then keeps the native source
skeleton at its authored origin and normalizes the imported mesh object
transforms into that skeleton space. The selected and rejected mesh spaces,
fit residuals, and normalization matrix are recorded under
`native_*_import.bind_alignment` in the audit JSON.

## Preservation contract

The staging script does not modify any singer vertex position, material,
proportion, or existing vertex-group weight. Imported mesh object transforms
are normalized once into the factual native skeleton space; after that
normalization, the graft does not modify any existing singer bone matrix,
parent, or object transform. It adds only:

- the donor's exact finger-bone names beneath the singer's factual hand bones;
- singer-hand-space retargeted bind positions for those new bones;
- empty matching vertex groups on the singer's existing skinned meshes; and
- a hidden donor reference plus a machine-readable audit report.

An explicitly requested role-only subtree may also be removed from the unique
derived guitarist asset with `-ExcludeSubtreeRoot`. This never edits the source
MILO. For the singer-as-guitarist conversion, excluding
`bone_pos_mic.mesh` removes that complete authored subtree, including
`mic_stand.mesh`, from the saved `.blend` rather than relying on runtime hiding.

`bounds` mode maps the donor hand bounds onto the singer's weighted rigid-hand
bounds independently on each hand-local axis. This sizes and positions only the
new finger chains. It never scales or reshapes the singer body, arms, hands, or
geometry.

## From loose MILOs

Build PikminGuts92 Grim's `mesh_tool` (its default feature set includes the
experimental `milo2gltf` command):

```powershell
cargo build --release -p mesh_tool
```

Build the native GH2 rig-sidecar helper:

```powershell
cmake -S .\tools\character_rig_stage -B .\tools\character_rig_stage\build -A x64
cmake --build .\tools\character_rig_stage\build --config Release --target milo_rig_export
```

Then run:

```powershell
.\tools\character_rig_stage\stage_from_milo.ps1 `
  -SingerMilo C:\path\singer.milo_ps2 `
  -DonorMilo C:\path\guitarist.milo_ps2 `
  -OutputBlend C:\work\singer-finger-stage.blend `
  -Milo2GltfExe C:\tools\mesh_tool.exe `
  -RigExportExe .\tools\character_rig_stage\build\Release\milo_rig_export.exe `
  -ExcludeSubtreeRoot bone_pos_mic.mesh
```

If either character's model directory references an external skeleton or
geometry directory, pass those factual dependencies with `-SingerExtraMilo`
or `-DonorExtraMilo`. They are forwarded to Grim as additional source MILOs;
the staging script still modifies only the imported singer armature.

The wrapper converts both sources to temporary glTF scenes with Grim (either
`.gltf` plus its adjacent buffers/textures or `.glb`), invokes Blender
headlessly, and removes the complete temporary conversion trees. The `.blend`
and adjacent `.finger-graft.json` are the retained deliverables.

Grim follows the MILO's active View/LOD graph. The saved staging file therefore
contains the source-visible active meshes used for close finger-weight work;
source skinned meshes omitted by that authored View (normally lower LODs) are
listed verbatim in `native_*_import.omitted_source_skinned_meshes` in the audit
report. They are not silently fabricated or claimed as staged.

## From existing GLBs

```powershell
& 'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe' `
  --background `
  --python-exit-code 1 `
  --python .\tools\character_rig_stage\stage_finger_rig.py -- `
  --singer C:\work\singer.glb `
  --donor C:\work\guitarist.glb `
  --output C:\work\singer-finger-stage.blend
```

Open the staged file, unhide the donor if useful, and paint/tune only the new
finger vertex groups. The script deliberately refuses to replace existing
finger chains or proceed when hand bounds, parents, or armatures are ambiguous.

## Publishing boundary

Community `glTFMilo` is useful source for glTF skin ingestion, but its current
targets are later Xbox/PS3 titles. Do not round-trip the complete singer through
it for GH2 PS2. After the weights are approved, publish only the changed
skeleton/mesh skin facts through this project's established GH2 writer so the
singer's original materials, morphs, character objects, and other MILO data are
preserved.
