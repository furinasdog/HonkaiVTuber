# PMX physics

PMX reading remains in the existing VRM4U/Assimp importer. The fork retains raw rigid bodies, joints, modes, collision masks, limits and springs in `UVrmPmxPhysicsData`. A model-owned animation proxy supplies the complete animated skeleton to the [UMT native Bullet fork](https://github.com/furinasdog/UMTNativePlugin/tree/honkai-rendering/current-world-snapshot), then applies the simulated pose before rendering. This is not a conversion to VRM spring chains.

## Prepare and build

Initialize submodules and run `Scripts/Prepare-PmxPhysics.ps1` before building the UE target. The script reuses installed CMake, Ninja and MSVC; it does not install tools or change persistent environment settings. Existing fetched Bullet sources are reused. On a fresh checkout, UMT's own CMake obtains its pinned Bullet 2.75 archive and verifies its hash.

The bundled archive is `Tools/UMTNativePlugin/build-static-ue144/bundled/UMTNativePlugin.lib`. VRM4U enables this optional runtime on Windows x64 when that archive is present. Other builds retain the importer and report that PMX Bullet is unavailable, rather than pretending that static colliders provide simulation. UE 5.8.4 on this machine uses MSVC 14.44; other toolchains require a matching rebuild. The library and fetched Bullet sources are excluded from Git.

## Data and scope

PMX raw collision-mask bits are passed directly to Bullet: a set bit permits that collision group. They must not be complemented. Static/following mode 0, physics mode 1, and mode 2 with an animated local position are supported. Existing UMT joint types 0/1 are supported; unsupported joints, invalid indices or non-finite data reject initialization explicitly. Source data is preserved.

The runtime uses fixed 60 Hz simulation, bounded substeps, resets after a long frame, and rebuilds for model or uniform-scale changes. Kinematic colliders follow the current animated body; physics controls hair and clothing only where the source PMX supplies suitable rigid bodies, joints and skin weights. It does not infer cloth or hair physics for models lacking those data.

Bone references follow the importer's same name-normalization rule, including PMX names containing `+`. The fork adds a current-world transform snapshot for simulation feedback. The older interpolated motion-state API remains available; using it to reposition current Mode2 bodies introduces a time mismatch. The native regression retains that old feedback as a failing comparison.

Local four-model tests cover 10 seconds static, 15 seconds moving and 20 seconds varying frame intervals with three resets. March 7th, Sparkle and Senti meet the test's maximum 5 cm / P95 1 cm joint-residual targets. Phainon's cross-chain Mode2 cape constraints still exceed those targets (about 10–11 cm maximum in static/moving trials). No per-character solver settings, disabled collisions or modified PMX data conceal this failure. These are diagnostic constraint measurements, not certification of visual collision quality.

Native fixtures and UE automation cover gravity, reset, collision groups, complete pose output, sparse compact indices and mode 2 under a simulated parent. Those checks do not establish collision-free appearance or fidelity to official game physics. Model-specific visual validation and live tracking remain separate checks.

UMT is MIT, copyright Candidum Games. Bullet is distributed under its original zlib license; see `ThirdParty/Bullet-LICENSE.txt`. These licenses do not replace model or Unreal Engine licenses.
