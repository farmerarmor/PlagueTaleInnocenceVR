# Controls and diagnostics

Shortcuts require game keyboard focus. Settings persist in PlagueTaleVR.ini.

| Key | Action |
| --- | --- |
| F2 | Toggle first-person camera |
| Shift + F2 | Toggle local first-person head hiding |
| F3 | Cycle Off, DLSS Quality, Balanced, Performance, Ultra Performance |
| F4 | Compare aim-indicator correction |
| F5 | Toggle surface decals |
| Shift + F5 | Record native GPU interval timings |
| F6 | Compare gameplay-camera head follow |
| F7 | Compare furnace/projected-light bounds correction |
| Shift + F7 | Compare tiled-light-list correction |
| F8 | Capture both native eyes, GPU bindings and mod state |
| F9 | Recenter headset tracking and reanchor menu screen |
| F10 | End VR and return to flat rendering |
| End | Enable shader hunting |
| Page Up / Page Down | Browse shaders and hide the current selection |
| Delete | Mark the selected shader and capture |
| Home | Restore hidden shaders and disable hunting |

Keep FollowHead, LightListMode, ProjectedLightBoundsMode and UI corrections enabled for normal play. Comparison controls diagnose problems. Captures and timing alter the workload; avoid F8 during a performance comparison.

The INI exposes first-person eye height, forward and side offsets in metres. These are visual offsets, without roomscale collision or automatic crouch adaptation.
