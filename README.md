# A Plague Tale: Innocence VR

A Windows OpenXR mod for **A Plague Tale: Innocence**, using the game's native left and right eye renders from the same game frame, with headset rotation and positional tracking.

[Download v0.1.0](https://github.com/farmerarmor/PlagueTaleInnocenceVR/releases/tag/v0.1.0)

## Install and play

1. Connect your headset to PC VR and select its OpenXR runtime.
2. Extract the complete release ZIP to a writable folder. Keep the included files together.
3. Close the game, then run **LaunchVR.cmd**. Select the game folder if requested.
4. Load gameplay, put on the headset and press **F9** to recenter.

The launcher starts the game and VR companion together, uses the headset's recommended per-eye resolution, and sets game scaling to 100%. It backs up the original resolution/scaling in the mod folder and does not replace game files.

**Defaults: third person and DLSS Balanced.** Initial menus render natively; DLSS starts automatically after ten seconds of stable tracked gameplay. A supported NVIDIA RTX GPU is required for DLSS. If initialization fails, native rendering remains available.

Movement and aiming use the game's mouse/controller controls. Turning your head does not steer walking direction. Reticles follow the mouse/controller aim. Motion-controller aiming is not implemented.

## Controls

| Key | Action |
| --- | --- |
| F2 | First person / third person |
| Shift + F2 | Compare first-person head hiding |
| F3 | Cycle Off, Quality, Balanced, Performance, Ultra Performance |
| F5 | Toggle optional surface decals |
| F8 | Save a diagnostic capture |
| F9 | Recenter; reposition an open menu screen |
| F10 | Stop VR and restore flat rendering |

F3 briefly shows the selected mode in the headset. Used presets keep separate cached DLSS histories until the game exits, increasing VRAM use when cycling modes. DLAA is not included. Inventory, pause and options menus appear on an anchored virtual screen; closing them restores tracked stereo gameplay.

The optional shader hunter uses **End** to enable, **Page Up/Down** to browse/hide a shader, **Delete** to mark/capture and **Home** to restore all shaders and disable hunting. See [SHADER-HUNTER.md](tools/SHADER-HUNTER.md) and [CONTROLS.md](CONTROLS.md). Normal play does not require hunting.

**StopVR.cmd** ends VR through the companion's stop flag. After closing the game, **RestoreResolution.cmd** restores only saved resolution/scaling values and preserves other settings.

## Rendering and first person

The mod includes per-eye temporal histories, tracked visibility/camera updates, corrected tiled/projected lighting and binocular HUD/aim corrections. The default profile disables effects that caused flashing in tested scenes. Surface decals are disabled by default and can be restored with F5.

Experimental first person follows Amicia's player root. Her head and hair are hidden only in the local first-person scene view. Teeth and hands share a shader, so oral hiding identifies the mouth mesh separately and preserves her hands. Third person restores the complete character.

First person currently uses a standing eye-height offset. Automatic crouch-height adaptation, roomscale collision, other outfits/mesh LOD variants and broad cinematic coverage remain unfinished or unverified.

## Game versions

Tested on GOG 1.0.4 x64/D3D11 with Oculus OpenXR. **Other game versions may attempt startup; there is no executable hash/version whitelist.** Native integration still requires compatible code/data at the hook locations. If those checks do not match, the mod does not install the affected hooks. Other stores/versions have not been verified.

Headset tests cover the current camera, lighting, menus, aiming, DLSS mode cycling and first-person hand/mouth fix in the tested scenes. Compatibility with every chapter, outfit, GPU and headset is not established. Performance varies by scene and render resolution.

## Build from source

Requirements: Windows x64, Visual Studio 2022 C++ tools, CMake 3.24+, PowerShell and Python 3 for OpenXR SDK configuration.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\FetchDependencies.ps1
.\build.cmd
powershell -ExecutionPolicy Bypass -File .\package\BuildPackage.ps1
```

Dependencies are pinned and checksum-verified in `dependencies.json`: OpenXR SDK 1.1.60 and the official NVIDIA DLSS SDK. NVIDIA headers, libraries and DLL are fetched from NVIDIA and excluded from this source repository. Their separate license applies. An existing OpenXR checkout may be selected with `OPENXR_SDK_SOURCE`.

Runtime binaries go to `build-release/bin`; the package and SHA256 file go to `dist/release`. `build.cmd <target>` builds an individual validation target. Some GPU validation tools require private captures from a legally owned game; these inputs are not redistributed. F8 captures are excluded from the repository and release.

## License

Original mod source: [LGPL 2.1](LICENSE). Third-party components retain their own licenses; see [THIRD_PARTY.md](THIRD_PARTY.md). NVIDIA DLSS is separately licensed and excluded from the LGPL grant. This unofficial mod includes no game assets.
