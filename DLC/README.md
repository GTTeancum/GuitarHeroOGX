# DLC packages

Place each package in `DLC/<package-id>/manifest.json`. The runtime loads the
base GH2 DTBs first, then manifests in deterministic folder-name order. Set
`GHOGX_ADDONS_DIR` only when an installation needs a different package root.

```text
DLC/
  package.id/
    manifest.json
    content/
      char/...
      config/...
      songs/...
      track/...
      ui/...
      world/...
```

`content/` mirrors virtual ARK paths. A file at
`content/char/example/og/gen/example.milo_ps2` is therefore requested as
`char/example/og/gen/example.milo_ps2` by the normal loader. Loose mounts are
process-wide, so menus, gameplay, textures, audio, songs, characters, venues,
and instruments all use the same lookup contract without modifying the user's
ARK.

Schema version 1 supports any combination of `characters`, top-level
`outfits`, `guitars`, top-level `finishes`, `venues`, `songs`, and `setlists`
in one manifest. A package can be one new character, an outfit expansion for a
built-in character, a venue, one song, a setlist pack, or a combined release.
Character outfits may opt into skeleton retargeting with
`animation_source_model`, `retarget_animation`, and role-specific
`guitarist_hidden_roots`.

A character or individual outfit may declare a source-authored fallback guitar
with `preferred_guitar`, `preferred_guitar_finish`,
`preferred_guitar_paint_primary`, and `preferred_guitar_paint_secondary`.
Outfit values override character values. The fallback is used only when the
player has not selected a guitar and the named instrument exists; an explicit
player preference always wins, and an unavailable add-on instrument falls back
to the normal guitar selection path.

Disc-imported song packs use `song_catalogs`, an array of indexed paths to
source `songs.dtb` files within the package. The runtime appends every complete
source record to the base GH2 table and exposes it in Quickplay. This preserves
stem layout, preview, practice, rank, venue, band, and other authored fields;
it does not replace `config/gen/songs.dtb`. Duplicate song IDs reject the
package transactionally.

Importer-authored song manifests may also declare `source_game`,
`source_routes`, and `source_default_bands`. These preserve source-authored
song records while routing their character, venue, instrument, and band IDs to
the corresponding namespaced loose assets. The GH1 installer emits only the
song catalog and song files from user media; converted GH1 characters,
animations, and venues are supplied separately by the bundled
`project.gh1.converted` package.

Package IDs, mounted paths, selection IDs, and catalog IDs must be unique.
Replacing an existing ARK path is rejected unless that exact normalized path is
listed in `replaces`. Every referenced character asset must resolve either in
the package content tree or the base archive. A manifest is transactional: if
any later row is invalid, none of that package's earlier catalog or table
changes remain active. Standard JSON string escapes (including Unicode
surrogate pairs) and exponent-form numbers are accepted.

Installer-produced manifests contain a sorted `files` array listing every
ARK-relative path below `content/`. The runtime uses this as a cached loose-path
index and verifies that every indexed file exists. Hand-authored packages may
omit `files`; they retain the recursive-directory fallback. Duplicate, absolute,
escaping, or missing indexed paths reject the entire package.

New character and outfit descriptions are blank when omitted; the runtime does
not fabricate localization keys. Character order is base DTB order followed by
successfully loaded packages. Outfit order is manifest order, so place the
default outfit first. The built-in singer package uses GH2 first and GH1
second.

See `examples/everything/manifest.json` for the complete field layout. The
example folder is documentation only; copy it to a direct child of `DLC/` and
provide its referenced content before enabling it.
