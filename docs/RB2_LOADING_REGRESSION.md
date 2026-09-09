# RB2 loading regression restored — 2026-09-09

Penelope's recurring delay came from a stale compiled MILO parser library.
The September 8 boundary-recovery fix remained in the worktree but was not in
commit history. Its original deployment report is
`rb2_wii/RB2_PARSER_FIX_REPORT.json`; that deployment relinked an executable
without updating the reusable parser library. Later incremental builds reused
an older parser object/library whose timestamps were newer than the source.

The existing built test executable did not contain the boundary-work test.
Recompiling that test against the existing library reproduced
`large boundary recovery: superlinear directory boundary work` (exit 1).
Explicitly recompiling `milo.cpp` and relinking made the same test pass (exit 0).
This is a deterministic work-budget regression, not a new load-time benchmark.
The 1,000-object fixtures also check exact roundtrips, including embedded
markers; the suite preserves ambiguous-boundary and existing GH1 behavior.

The restored parser validates forced one-marker-per-object boundaries
iteratively. The fallback search skips impossible suffix partitions while
retaining candidate order and ambiguity detection. A progress callback supports
bounded-work validation and cooperative loading. Unrelated parsed-cache changes
are excluded from the fix's commit.

The live runtime was rebuilt and deployed, and a hidden native Manage Band run
resolved and loaded `rb2_penelope_mcqueen_default` successfully. Per the user's
instruction, no new timing comparison was performed. Penelope's model, textures,
rig, animation dependencies, and package index were not modified.

Build cleanup must not silently reinstate the stale parser object/library or
obsolete test executable. Those invalid cache outputs must be invalidated so a
future incremental build recompiles the committed fix and regression test.
