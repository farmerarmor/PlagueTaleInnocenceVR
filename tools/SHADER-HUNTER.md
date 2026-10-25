Plague Tale VR shader hunter — Probe25 and later

Load the affected area, wear the headset and give the game keyboard focus.
The hunter starts disabled.

- End: enable hunting and hide the first shader. This rebuilds the combined
  list with shaders used so far first, followed by all other captured creations.
- Page Down / Page Up: restore the previous shader and hide the next / previous
  shader. The list wraps and includes pixel, vertex, compute, geometry, hull
  and domain stages together. There is no stage filter.
- Delete: mark the selected shader for diagnosis and request a native eye-pair,
  pose and GPU-state capture. Marks are retained after Home.
- Home: restore every shader, reset the selection and disable hunting.

F9 recenters and F10 stops VR as before. No desktop selector or numpad controls
are needed. Only one shader identity is hidden at a time, across both eyes.

Coverage is cumulative across frames and contexts. While enabled, newly used
or created shaders append to the list without moving the current selection.
Press End again after changing area to put the used shaders first again.
All six creation paths, geometry stream-output creation, all seven graphics
draw forms and direct/indirect compute dispatch are covered, including deferred
contexts. The hunter reads the actual bound shaders at each command and does
not limit the list to one frame, one eye, a draw-count cap or successful shader
reflection. Unused captured shader creations are also included.

Shader bytecode created after injection is identified by its FNV-1a hash.
An already-created shader whose bytecode was unavailable remains huntable
using a unique object token, explicitly labeled in the catalog and marks.
The catalog covers shaders created or bound by this game process; shader
assets that the game has never loaded do not have a runnable shader object.

Hiding a material shader may remove the entire wall. Mark it if it helps
identify the affected material, but say whether the wall also disappeared.
Suppressing compute or an offscreen producer may leave an old intermediate
image until rendering resumes; Home restores all shader commands.

Private diagnostics beside this runtime:

- shader-hunter-catalog.json: combined cumulative list, stage names, identity
  kind, both-eye/outside-eye command counts, targets and resource bindings.
- shader-hunter-status.json: total, selected index/hash/stage, registered and
  observed counts for each stage, acknowledged keyboard state and frame.
- shader-hunter-marks.jsonl: Delete marks with shader, bindings, frame and pose.
- shader-hunter.log: selection changes.
- shader-hunter-hooks.log: additional creation/context hook coverage.

The accidental Probe24 mark 26df0c8f3eb4b48c is ignored. Fresh packages have
no imported marks. The later deliberate mark 7d6c9892c70a07a6 identified the
tiled-light producer; Probe26's full-projection correction was confirmed in
the headset to fix the moving window lighting without new problems.
The packaged profile enables that correction with LightListMode=1. Other
lighting comparisons remain off. The accepted native world camera, headset
resolution, histories, effect defaults and UI correction remain enabled.
