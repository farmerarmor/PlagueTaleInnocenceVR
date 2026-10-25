# Third-party components

The original mod source is distributed under LGPL 2.1; see LICENSE. Separately licensed components retain their original notices.

- MinHook: BSD-style license; vendor/minhook/LICENSE.txt.
- NativeBridge, camera utilities and transport: LGPL 2.1; vendor/stereo-bridge/LICENSE. Adapted from the author's earlier native-stereo bridge work. Game integration is specific to A Plague Tale: Innocence.
- Khronos OpenXR SDK/loader 1.1.60: Apache 2.0; vendor/OpenXR-LICENSE. The pinned dependency is downloaded by scripts/FetchDependencies.ps1 and the loader is linked into the host.
- JsonCpp, linked by the OpenXR loader: MIT license, included as licenses/JsonCpp-LICENSE.txt in the runtime package.
- Microsoft DXBC hash implementation: University of Illinois/NCSA license; vendor/dxbc-hash/LICENSE.TXT. Used to validate the modified head-material shaders. Original attribution remains in DxilHash.cpp.
- NVIDIA DLSS/NGX: separate NVIDIA RTX SDK license; vendor/dlss/LICENSE.txt. The unmodified SDK runtime is included with the compiled mod under that license. SDK headers/libraries are fetched from NVIDIA's pinned official repository when building and are not included in this source repository. NVIDIA components are excluded from the mod's LGPL grant. This software contains source code provided by NVIDIA Corporation. The mod is not sponsored or endorsed by NVIDIA.

The runtime package includes the applicable third-party licenses. No game executables, extracted game shaders, textures, meshes, captures or other game assets are distributed.
