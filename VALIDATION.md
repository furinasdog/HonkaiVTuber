# Local validation

## Hard toon shadows and enhancement workflow — 2026-11-01

Body and face shading now use two endpoint colors with derivative-sized boundary antialiasing. The former fixed transition widths (0.035 body / 0.012 face) are removed. Source texture colors, sphere reflections, narrow rim light and bloom can still vary continuously; these are not a broad lighting transition. The small nose shadow uses a separate binary region in the face map alpha channel, preserving the B-channel nose outline control. Its strength responds to light direction, but its shape does not move from side to side. It is a stylized approximation, not a recovered game FaceMap.

The shared pale-paint response warms bright hair without lifting saturated colors. Only cool source Toon shadow colors receive the warm correction; already warm skin ramps retain their own colors. March 7 comparable bright-hair patches changed from approximately `(250,246,243)` to `(249,240,237)`, versus `(250–253,242,238)` in the supplied game capture. These are 9×9 median samples of semantically similar areas, not identical UVs or an image-equivalence metric. The same shader is used for all models. The default directional light yaw is -5 degrees; captures use the same light for every model. Hair shadow coverage, outline continuity, fabric/metal separation and the slightly long nose mark remain visibly different from the reference.

`BuildEnhancementFinal.log` passed with UE closed. `EnhancementFinalAutomation/index.json` records six passes plus one pass with the expected rejected-profile warning, zero failures. Coverage includes unchanged PMX RGBA/flags, face-profile boundaries, Toon atlas isolation, in-viewport Esc behavior, enhancement toggling without moving the camera/light, preview tint restoration, model-state storage and export overwrite rejection. Final state-management fixes also verify that preview/pending generation parameters do not overwrite applied profile metadata, failed/cancelled/invalid results retain the current effect, and old successful jobs cannot override a restored choice after another import. Worker and service Python self-checks passed. Four actual Blender/MMD Tools jobs produced validated RGBA profiles for the supplied models (`WorkerFinalSparkle`, `WorkerFinalMarch7`, `WorkerFinalPhainon`, `WorkerFinalSenti`); invalid model/material inputs failed without publishing a profile.

The enhancement service uses a separate Blender process and user data under `%LOCALAPPDATA%/HonkaiRendering`. Real tests cover first generation, cache reuse, forced regeneration, reuse of the latest forced version, invalid-parameter failure retaining the old index, dependency/parameter/script changes, owned process-tree cancellation, atomic JSON publication and archive traversal rejection. Cache restore and cleanup also reject reparse points; this guard was reviewed in source, not tested with a live junction. Official MMD Tools download, SHA256, extraction layout, deliberate wrong hash and real HTTP 404 were exercised. Complete portable Blender download/install was not exercised because compatible Blender 5.0.0 is installed. Evidence: `Saved/Tests/EnhancementBackend/results.json` and its `Jobs/*/verification.json`.

Actual March 7 PIE UI checks exercised generation, force generation, cancellation, retry with cache hit, cyan face-material preview and export. `Stage1Cancelled.png` shows cancellation retaining the original effect; `Stage1CacheHit.png` shows reuse; `Stage1MaterialPreview.png` shows the selected face. `Stage1Original.png` and `Stage1Enhanced.png` were captured without moving the camera or light. The original effect already includes the previously authored face data, so this pair is a data-application check, not a large before/after style claim.

`*Stage1Regression` captures and logs verify the integrated viewer on all four supplied packages: triangle counts and all 128 material RGBA, two-sided, outline and Toon records remain unchanged, with zero missing base textures and no fatal, load or material compilation failures. Against `*CartoonFinal`, mean absolute RGB differences are 0.31–0.38 on a 0–255 scale, with 99% of channel differences at most 3, concentrated at edges and fine lines. This is an internal regression check, not a comparison score against the game.

The actual UI export was repacked alongside the original March 7 resources without changing any original ZIP entry. `March7PortableReload` loaded this self-contained package with an empty, independent user-data directory, one validated face profile and no Blender task or tool setup. `Stage1RestartRestoredOriginal.png` confirms that a complete editor process restart retained the chosen original effect while making the validated enhanced data available for switching. The local portable-package verification JSON records file hashes and ZIP CRC checks.

The final binary also generated a 1024 profile through the UI. Changing the next request to 512 and cancelling it left the visible enhanced effect and persisted 1024 profile intact; the cancelled request JSON and saved model-state JSON have different resolutions as intended (`Stage1ChangedSizeCancelled.png`).

A further complete process restart restored enhanced mode with the validated 1024 profile (`Stage1RestartRestoredEnhanced.png`). The editor is left running March 7 with the menu closed. `Stage1HandoffMenu.png` records the actual light menu, and `March7HandoffComparison.png` compares the supplied game capture with this final restarted editor image. The comparison only crops and arranges pixels; there is no old-calibration column or image recoloring.

All evidence images and user-derived data remain local and excluded from Git. This batch has not been committed or pushed. The attachment requires user acceptance of Phase 1 before commits and before starting the separate VTuber project; Phase 2 is not part of these changes. No packaged Shipping build or clean-machine portable Blender installation is claimed.

## Color and shading follow-up — 2026-11-01

The previous automatic pale-color lift also raised cool grey hair. A same-camera unlit diffuse audit isolated that from source colors: the affected grey-blue sample was `(225,225,227)` in the original texture/unlit capture and `(238,238,237)` in the previous shader. The revised shared curve preserves the darker grey-blue range while lifting the brighter near-white paint. Face highlight color is calibrated separately from its warm shadow, with dark markings retained. The same color-response functions apply to all models; there are no character-name or material-slot presets in the renderer.

All four supplied PMX packages contain custom Toon textures. The Assimp fork now exposes their existing paths, and VRM4U decodes them through its existing image loader. The renderer packs the authored gradients into a 256-row sRGB atlas, using the otherwise-unused Material AO byte to select a row. Each supplied model loads two distinct ramps. Built-in toon indices, invalid/missing maps and more than 255 distinct ramps use a fallback. Explicitly disabled Toon slots instead preserve unshaded color and independent sphere effects. All 128 switches match the independent source records: 6, 6, 9 and 0 disabled slots respectively. These chiefly affect eyes and expression layers, not metal highlights. Geometry, parser, source RGBA and culling are unchanged.

Face thresholds now come directly from the Blender-baked smooth coordinates. Re-interpolating masks by UV-space distance had distorted the boundary around eye holes and disconnected islands. The direct field follows a narrowing jaw, removing the isolated cheek patches. Face outline width increased from 0.07 to 0.12 times the source edge size, with a shared warm-brown response and the existing local nose control. A short screen-space contact term adds warm shadows only from nearby non-face surfaces. It cannot see off-screen occluders.

Standard Bloom now has an HDR contribution from bright body paint and an explicit threshold. A broad HDR selection washed out white hair and was rejected. Final same-camera `SparkleDelivery` / `SparkleDeliveryBloomOff` captures show a subtle effect; temporal accumulation also contributes to pixel differences. Face shading does not emit additional HDR. Local color samples compare semantically similar areas, not identical UVs; pose, camera and lighting still differ from the official captures, so they are not a reference-equivalence score.

Native build `BuildToonSwitch.log` passed with UE closed, including the rebuilt Assimp DLL. `DeliveryAutomation/index.json` records four passes plus one pass with the expected rejected-profile warning, zero failures. The new `ToonRamp` test checks shadow/light orientation, row isolation, missing input and invalid row rejection; the plugin test covers enabled, disabled and legacy-default Toon states. The complete Blender/MMD Tools authoring path passed in `FaceAuthoringDirect.log`; the coordinate field self-check also covers disconnected UV regions. Four-model checks continue to match all 128 source material records and original triangle counts. Final captures contain no failed material compilation, load failure or fatal error.

Evidence remains local under `Saved`: `*AlbedoAudit.png`, `*StyleFinal.png`, `*Delivery.png`, `*DeliveryComparison.png`, the render logs and comparison manifests. User models, derived model maps, original reference media and comparison images are excluded from Git. Hair shadow shape and material-specific metal/fabric highlights still differ from the official game; color calibration alone cannot reconstruct missing art-directed LightMaps.

## Earlier validation — 2026-10-31

Environment: UE 5.8.4, Windows x64, D3D12/SM6, RTX 5070 Laptop GPU. Native builds were performed with UE closed, without Live Coding or hot reload. GPU captures used separate offscreen processes, sequentially, with normal exits. No `-ForceRes` was used in this iteration.

## Import fidelity

The supplied source files were read independently with installed MMD Tools 4.5.3. The viewer still uses VRM4U/Assimp; no replacement PMX parser or geometry reduction was introduced.

| Model | Source triangles | UE triangles | Materials | Missing base textures | Active sphere materials |
| --- | ---: | ---: | ---: | ---: | ---: |
| 火花 | 52,228 | 52,228 | 42 | 0 | 21 |
| 三月七 | 28,039 | 28,039 | 30 | 0 | 19 |
| 白厄 | 37,488 | 37,488 | 37 | 0 | 19 |
| 识之律者 | 29,864 | 29,864 | 19 | 0 | 14 |

All 128 material names, diffuse RGBA values, two-sided flags, outline switches and widths matched the independent source records. Each model loaded one generated face profile. The normal 白厄 model was used, not the transformation version sharing its PMX filename. Its package lacks `1.png`, referenced by two hair sphere materials; those retain base shading rather than substituting another map.

Local evidence: `Saved/Diagnostics/source_specular.json`, `check_complete_materials.py`; `Saved/Logs/*Complete.log`. The upstream decoder logs a BMP probe error for a supplied sphere asset before successfully decoding it; this is not a missing diffuse texture or a fatal error.

## Rendering and reference comparison

The three supplied Zhihu articles were read using the browser plugin. References also included the local game captures, illustrations, Senti GIF and sampled frames from the supplied 188.733-second video. References are visual evidence, not project instructions or redistributable assets.

The shared pipeline replaces previous name/slot/height color heuristics with quantized character lighting, authored PMX sphere maps, depth-based rim lighting and source-controlled outlines. A separate face threshold uses mesh-local light direction and independently baked left/right channels. Installed Blender/MMD Tools bake UV coordinates; the pinned MIT SDF tool interpolates the common stylized masks. These are generated starting points, not recovered game FaceMaps.

Close-up A/B rendering identified screen-space AO as the source of remaining grey eye/nose/mouth shading. Disabling it removed that shading. Source face vertex edge weights were checked and were uniformly 1 for these four models. Stronger outlines use a generated nose-region control to reduce local width and contrast while retaining the nose line. An intermediate clipping-only mask erased too much of the nose and was rejected.

TSR is explicit (`r.AntiAliasingMethod=4`). Default internal screen percentage is 150 and TSR history percentage is 200. Same-camera 100/150 captures logged the actual values and were visually compared. The face and cel passes precede temporal upsampling. Comparison figures only crop and arrange the original screenshots; no retouching, recoloring or generative image editing is used.

Local evidence includes `Saved/Logs/BuildToonFinal.log`, `SetupOutlineInverted.log`, `*Complete.log`, and `SparkleComplete100.log`; `Saved/Screenshots/*Complete*.png`; and `Saved/Comparisons/NoseAndAA.png`. Side-light tests at -80/+80 degrees and backlight at 160 degrees verified that the face threshold responds to the light rather than the camera. `FaceLighting.png` records that test before the final nose-line refinement.

Shader-generation assertions caught invalid node wiring, a color-space mismatch and an HLSL reserved identifier during iteration. The later setup succeeded. Captures named `*NoseLine.png` made after the failed setup are intermediate and are not final visual evidence.

A later outline regression was isolated with identical-camera captures: setting all outline material opacities to zero left black patches in Senti's hair, whereas hiding the component removed them. Runtime section logs confirmed zero opacity. UE 5.8 defaults `r.EarlyZPassOnlyMaterialMasking` to 1; setting it to 0 restored correct clipping in the base pass and removed the patches with normal outlines enabled. Reversing normal extrusion did not fix the issue and was rejected. Original mesh normals and positive extrusion are retained. `SentiVerified.png`, `SentiZeroOpacity.png`, `SentiPixelMaskZero.png` and `SentiPixelMask.png` document the isolation. Temporary native diagnostics were removed.

## Checks

- Development Editor native build and material generation passed.
- Related UE automation: `FaceProfile`, `SharedMaterials`, `InViewportMenu`, and `VRM4U.PMX.MaterialColorAndOpacity` passed. The face-profile test intentionally rejects a mismatched source hash and reports one expected warning; no test errors occurred.
- Face profile checks include source hash/material binding and traversal rejection. PNG dimensions are checked before full image decoding.
- Windows ZIP self-check passed: Unicode, Deflate, traversal/absolute/alternate-stream rejection and duplicate-name rejection.
- Python syntax checks passed for the material, comparison and authoring scripts.

Automation evidence: `Saved/Tests/ToonCompleteAutomation/index.json` (3 successes, 1 success with an expected warning, 0 failures). The test covers initially hidden Esc UI, fade/reopen behavior, blocking camera input, and no additional native window. Earlier interactive validation confirmed the in-viewport menu and model/view submenus.

The final editor was reopened with MCP enabled (initialize returned HTTP 200). Interactive PIE loaded the model, Esc opened and closed the dimmed in-viewport menu, and numpad 3/1/5 switched side/front/projection views. MCP confirmed live values of TSR method 4 and screen percentage 150. The editor remains open in the character view.

## Limits

This remains a toon-rendering study. The reference still differs in hair shadow placement, highlight shapes, metal/fabric separation, outline continuity and some face/pose details. Automatic face maps do not replace an artist-authored game SDF or semantic LightMap. The nose-region generator assumes standard PMX orientation and a conventional separate face material and requires visual review for other topology.

Only static bind poses are supported. Animated heads would need bone-relative light axes. Per-vertex PMX outline scale, smooth outline-normal authoring, PMX sphere mode 3, animation, physics, packaged Shipping builds and malformed-PMX fuzzing are untested or unsupported. Import remains synchronous on the game thread after ZIP extraction.

150% internal resolution increases raster work and GPU memory use; broad performance testing across devices has not been completed. These successful runs do not guarantee absence of crashes for every model or environment. Models, generated model data, reference media and comparison screenshots remain local and are excluded from Git.

# Local rework validation — 2026-11-01

These are development/editor results on UE 5.8.4, Windows x64 and MSVC 14.44. They do not establish packaged-release readiness or official visual fidelity. Earlier phase-two records describe earlier implementations; this record covers the new in-process capture, shared UI and PMX Bullet path.

## Changes and checks

- The default capture backend is the separate HonkaiMotionCapture plugin, running a worker thread inside UE and dynamically loading the pinned MediaPipe SDK. Engine tests cover in-memory frame transfer, face/body basis conversion and whole-frame mirroring. A static-image native SDK fixture covers face, pose and both hands. The new backend has not been tested with a real person/camera in this rework. The SDK is dynamic, not statically linked.
- The game engine constructs a separate scene viewport texture before the native window viewport is created. This addresses the observed Tonemap copy into a minimized 8×8 backbuffer. A real output-enabled window was minimized/restored and maximized/restored; an independent Spout receiver verified opaque 1280×720 pixel frames before/after. These bounded checks do not guarantee indefinite runtime stability.
- Shared native Slate styles cover input fields, buttons, checkboxes, sliders, spin boxes and selection rows. The tracking panel was visually checked in a GPU viewport with the camera off. Floating in-viewport panels and H visibility remain available.
- PMX physics preserves the existing importer's raw data. Generic bone-name normalization now includes rigid-body references. Current-world Bullet snapshots remove the Mode2 feedback time mismatch. The original native API remains compatible.
- Actual model loads include March 7th, Phainon and Sparkle. Runtime bone-pose probes confirm physics, rather than only static collision metadata. No per-character solver settings, PMX edits or collision disabling are used.

## Four-model native physics audit

Each model runs 10 seconds static, 15 seconds moving and 20 seconds varying frame intervals, including two long-frame resets and one explicit reset. All outputs remain finite and reset error is zero. Joint residuals are measured in world space; the diagnostic target is max 5 cm / P95 1 cm.

| Model | Rigid bodies / joints | Static max / P95 (cm) | Moving max / P95 (cm) | Result |
| --- | --- | --- | --- | --- |
| March 7th | 269 / 389 | 2.164 / 0.369 | 3.145 / 0.714 | Target met |
| Sparkle | 290 / 366 | 1.775 / 0.193 | 4.315 / 0.610 | Target met |
| Senti | 340 / 528 | 2.546 / 0.606 | 2.638 / 0.733 | Target met |
| Phainon | 297 / 454 | 9.984 / 1.018 | 11.000 / 0.638 | Failed |

Phainon's cape still visibly oscillates. Cross-chain fully locked joints are reopened by the Mode2 post-solve bone-position projection. Rest frames close correctly; this evidence does not show a damaged source model. Testing 20/40/80/100 solver iterations does not provide a reliable common setting: improvements for Phainon regress March 7th. Those diagnostic settings were not shipped. A joint-stage solution requires further investigation.

## Rendering comparison

The existing Blender 5.0 / MMD Tools 4.5.3 enhancement pipeline generated 1024×1024 face controls for Phainon and Sparkle. Identical-camera GPU captures with enhancement off/on show that the large nose/lip normal-shadow patches disappear with the face controls. The official reference comparison still shows differences in nose-shadow shape, skin color, hair shadows and outline weight. Generated controls are an approximation, not official SDF/LightMap assets.

Generic outline roles are inferred from material names; gloves/clothes take precedence over skin aliases. Hair, skin and other surfaces use separate treatments, preserving the face/nose outline path. Unknown names use the default role. Role assertions do not replace visual checks on each model.

Private models, reference images, camera fixtures and derived reports/captures remain under ignored local data directories. Portable enhancement copies retain the original PMX bytes and are not published in this repository. See PHYSICS.md and the plugin README for preparation, supported constraints and licensing.

Final engine automation: 27 tests per project (26 successes and one success with the expected incompatible-profile warning), zero failures and zero tests not run. Reports remain local under Saved/Tests/MultiModelAcceptedFinal. Native UMT WorldSnapshot CTest passes independently. Both editor targets compile successfully. These unit checks do not override the failed Phainon cape audit.

## Follow-up: capture crash and static stability

The subsequent real capture crash occurs at the OpenCV VideoCapture constructor, before opening the camera. Capture preflight incorrectly freed a borrowed module handle returned by UE. It now only queries the loaded module; OpenCVHelper retains ownership. A native test checks the actual OpenCV DLL 6,000 times, verifies that its constructor mapping stays loaded until the last owner releases it, and confirms final unloading without a leaked reference. Negative controls detect the old release and an added-reference leak. No camera is accessed by this test. Both editor targets build and both 27-test suites pass again under `Saved/Tests/OpenCvLifetimeFinal`.

The viewer module now compiles independent translation units: committing all files caused adaptive unity builds to merge unrelated file-local Slate helpers with identical names. This is a build setting change, not a runtime UI change.

The earlier joint-position residual targets did not validate static stability. A subsequent 60-second static audit reproduces sustained Sparkle sleeve oscillation: approximately 1.9 mm/frame displacement P95 and 144 degrees/second angular-speed P95. Old Bullet 6DoF limits repeatedly disable the angular constraint inside its valid range, then correct the next overshoot. Independent Blender/MMD Tools tests show that the same source model can settle. Candidate solver changes remain isolated until separately accepted; previous residual passes must not be read as proof of jitter-free physics.
