# Duke forearm check — 2026-09-09

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
