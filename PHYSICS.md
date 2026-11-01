# PMX physics

PMX reading remains in the existing VRM4U/Assimp importer. The fork retains raw rigid bodies, joints, modes, collision masks, limits and springs in `UVrmPmxPhysicsData`. A model-owned animation proxy supplies the complete animated skeleton to the [UMT native Bullet fork](https://github.com/furinasdog/UMTNativePlugin/tree/honkai-rendering/spring2-stability), then applies the simulated pose before rendering. This is not a conversion to VRM spring chains.

## Prepare and build

Initialize submodules and run `Scripts/Prepare-PmxPhysics.ps1` before building the UE target. The script reuses installed CMake, Ninja, Git and MSVC; it does not install tools or change persistent environment settings. It selects an installed Visual Studio instance containing the requested MSVC toolset and initializes its SDK environment when necessary.

UMT obtains an unmodified partial/sparse checkout of official Bullet 3.07 at commit `272c7099d3ba8ac9e8c142e361e8d4cf5d91b8f2`, including the original license. It verifies the exact commit and refuses dirty dependency sources. Existing Bullet 2.75 caches are preserved and are not reused. The official tag's internal version strings still say 3.05/305, so the commit identifies this dependency. A clean matching Git checkout can be supplied with `-BulletRoot`; otherwise sources are cached beneath the build directory.

The bundled archive is `Tools/UMTNativePlugin/build-static-ue144/bundled/UMTNativePlugin.lib`. VRM4U enables this optional runtime on Windows x64 when that archive is present. Other builds retain the importer and report that PMX Bullet is unavailable. UE 5.8.4 on this machine uses MSVC 14.44; other toolchains require a matching rebuild. Preparation builds/runs the native regressions and defaults to three compile workers. With the tested MSVC version, deterministic path/timestamp mapping permits the two projects to independently produce identical libraries. The library and fetched sources are excluded from Git.

## Data and scope

PMX raw collision-mask bits are passed directly to Bullet: a set bit permits that collision group. They must not be complemented. Joint-linked pairs are separately excluded from contact, following the mature MMD runtime convention used by Three.js; other pairs keep both original masks. This does not disable general collisions or modify source metadata. Static/following mode 0, physics mode 1, and mode 2 with an animated local position are supported. Existing UMT joint types 0/1 are supported; unsupported joints, invalid indices or non-finite data reject initialization explicitly.

Type-0 joints use Spring2 with all six axes enabled. Authored translation and rotation stiffness values are preserved, including zero stiffness; zero-stiffness axes still apply relative-velocity damping. Translation/rotation joint damping is `.5`, with Bullet's effective-mass/time-step safety limits. The old rotation-stiffness `lengthScale²` and inverse-square motor-damping conversions do not apply. Body damping remains the separately authored PMX value, clamped to Bullet's supported range at runtime. Equilibrium is captured before the first simulation step.

The margin setting is `1e-6` world metres, matching the mature import setup. Bullet retains shape-specific sphere/capsule radius behavior; this is not a uniform outward expansion of every collider. Additional low-speed damping and one redundant locked-translation pivot constraint remain enabled.

The runtime uses fixed 60 Hz simulation, bounded substeps, resets after a long frame, and rebuilds for model or uniform-scale changes. Kinematic colliders follow the current animated body; physics controls hair and clothing only where the source PMX supplies suitable rigid bodies, joints and skin weights. It does not infer cloth or hair physics for models lacking those data.

Bone references follow the importer's same name-normalization rule, including PMX names containing `+`. The fork adds a current-world transform snapshot for simulation feedback. The older interpolated motion-state API remains available; using it to reposition current Mode2 bodies introduces a time mismatch. The native regression retains that old feedback as a failing comparison.

The integrated production library (`3e588066ecc214f9b292e06e75c839d3fd912199c277877290f69a49d4a9f7c2` on the tested toolchain) was checked on complete March 7th, Sparkle, Senti and Phainon data, with 60 seconds static, 60 seconds global movement and 20 seconds variable-frame/reset simulation. All four met the maximum 5 cm / P95 1 cm primary joint-residual targets. Senti's locked-joint P95 was 1.0182 cm, slightly above the additional strict 1 cm target; that failure remains recorded rather than relaxed. Native finite/anchor/reset checks are not certification of visual collision quality or complete static convergence.

Mode2 still preserves animated local bone position and corrects the physical body using the current-world snapshot. The alternative that only copied rotation was measured separately and produced centimetre-scale collider/output-bone offsets, so it was not made the default. There are no per-character settings or modified PMX files.

Native regressions cover real zero-stiffness damping, resting at an angular stop, and linked-only contact suppression while a separate nonlinked contact remains active. Existing fixtures and UE automation cover gravity, reset, collision groups, complete pose output, sparse compact indices and Mode2 under a simulated parent. These checks do not establish collision-free appearance or fidelity to official game physics. GPU skinning, local shoulder/arm animation, live tracking and visible hair/clothing contact remain separate checks.

UMT is MIT, copyright Candidum Games. Bullet is distributed under its original zlib license; see `ThirdParty/Bullet-LICENSE.txt`. These licenses do not replace model or Unreal Engine licenses.
