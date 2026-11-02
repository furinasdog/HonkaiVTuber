# Third-party notices

## HonkaiRender Godot implementation

The motion retargeting, channel-response behavior and floating-panel interaction are adapted from [Honkai-Style-RealTime-Rendering](https://github.com/furinasdog/Honkai-Style-RealTime-Rendering), commit `a87e7d3cc5071ca867b4090ef3bb7a19a03a7652`, copyright (c) 2026 furinasdog, under MIT. The original copyright and MIT permission notice are retained by this project's LICENSE. Rendering and model importing continue to use this project's Unreal Engine pipeline.

## Spout2

`Plugins/Spout2_DX12` pins [GPUbrainStorm/UE5_Spout2_DX12](https://github.com/GPUbrainStorm/UE5_Spout2_DX12) v2.1.1, commit `145e71c53a2a8fc5c6e82dda0994520bba8eef65`, under MIT. Its bundled [Spout2 SDK](https://github.com/leadedge/Spout2) retains its BSD-2-Clause notices. Preserve both sets of notices when distributing.

## MediaPipe native tasks

The default tracker is the independently maintained [HonkaiMotionCapture](https://github.com/furinasdog/HonkaiMotionCapture) MIT plugin, linked as a pinned submodule. It loads the official MediaPipe 0.10.35 C API DLL inside the UE process and uses UE's OpenCV module. SDK/model binaries remain outside Git and are staged only after hash verification. MediaPipe and OpenCV retain their Apache-2.0 and third-party notices in the plugin. The prebuilt SDK includes usage telemetry; this in-process backend does not provide AppContainer network isolation. The old native EXE/AppContainer implementation remains a clearly reported fallback when the integrated backend cannot be prepared; its historical measurements do not validate the new backend. No new image-upload interface is added by the plugin.

## VRM4U

MMD import uses [VRM4U](https://github.com/ruyo/VRM4U), copyright Haruyoshi Yamamoto, under its MIT license. The project uses [the HonkaiRendering fork](https://github.com/furinasdog/VRM4U) as the `Plugins/VRM4U` Git submodule. The main repository pins the precise fork commit rather than an unversioned download.

The fork starts from upstream tag `v1.2026.10.17` (`7395b50e9aa50245c8677132d805e8a8eff10093`) and adapts PMX LOD0 and material-property preservation. Its PMX parser is unchanged. Upstream license files and Assimp/other dependency notices remain in the submodule and must accompany redistribution. See the fork's `HONKAI_RENDERING.md` for the adaptation.

## Assimp

The VRM4U fork includes a rebuilt Assimp Windows library and pins [the adapted Assimp source](https://github.com/furinasdog/assimp) at `ThirdParty/assimp-source`. The source baseline is `ruyo/assimp` commit `06c4e29a3c873648a107de9c1ac50f687b4d50d8`. The change exports existing PMX culling, outline, sphere and custom toon fields; it does not replace the parser. Assimp retains its BSD 3-clause license; see the source `LICENSE` and bundled `ThirdParty/assimp/LICENSE`. Source initialization and build instructions are documented in the VRM4U fork.

## SDF authoring tool

`Tools/sdf-shadow-threshold-map` pins the unmodified [SDF Shadow Threshold Map tool](https://github.com/akasaki1211/sdf_shadow_threshold_map), copyright 2024 Hiroyuki Akasaki, under MIT. Its license remains in the submodule. It is available for artist-authored masks; automatic face generation uses direct coordinate thresholds to avoid UV-island distance artifacts. The offline scripts use separately installed NumPy/OpenCV and Blender/MMD Tools to read PMX and bake UV coordinates. Those applications retain their own licenses and are not bundled or linked into the UE application. Generated model data remains local and is subject to the source model's terms.

## Unreal Engine templates

Existing template files under `Source/HonkaiRendering` retain their Epic Games copyright notices. Unreal Engine and template-derived code are subject to their applicable Epic license, not a replacement MIT license from this repository. The engine installation and engine binaries are not included here.

`Content/MmdViewer/T_LinearWhite` is a linear-sampling copy of Unreal's white fallback texture and retains the applicable Epic content terms.

## Reference media

User-provided MMD models, textures, game screenshots and illustrations are separate local reference assets. They are excluded from this repository, CI and cooked application; their original usage terms apply. The repository does not grant redistribution rights to miHoYo/HoYoverse content or third-party models.


## PMX Bullet runtime

`Tools/UMTNativePlugin` pins [our adapted UMTNativePlugin fork](https://github.com/furinasdog/UMTNativePlugin/tree/honkai-rendering/spring2-stability), based on upstream `cf0d4c857044e29ba9ee90c915577707ecbeae3a`; the exact fork revision is recorded in the Git submodule entry. It adds current-world snapshots and Spring2 parameter/contact adaptation with native regressions, retaining the original interpolated API. Its MIT license and Candidum Games copyright remain in that submodule. It builds unmodified Bullet 3.07 source at `272c7099d3ba8ac9e8c142e361e8d4cf5d91b8f2` under its original zlib license; see `ThirdParty/Bullet-LICENSE.txt` and the fork's `ThirdParty/BULLET_SOURCE.md`. The project adapts the existing PMX importer rather than replacing PMX parsing. The native archive is built locally and excluded from Git. See `PHYSICS.md` for preparation and supported constraints.

The VRM4U fork's `Source/VRM4U/Private/VrmPmxBoneTransform.cpp` adapts `MMDBoneTransform.BoneMath` from [Unity MMD Tools](https://github.com/CandidumGames/UnityMMDTools/blob/db35d9cb80ad57a8b2cbd40ad737a3c7bbe2d6c4/Runtime/MMDRuntime/MMDBoneTransform.cs), commit `db35d9cb80ad57a8b2cbd40ad737a3c7bbe2d6c4`, copyright (c) 2025-present Candidum Games, under MIT. The original copyright and permission notice are retained in `Plugins/VRM4U/LICENSE_UMT`. Include that notice with distributions of the adapted code and executable, alongside the VRM4U, UMT native runtime and Bullet notices above.
