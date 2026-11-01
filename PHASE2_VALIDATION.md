# Phase 2 preview validation — 2026-11-01

Machine: UE 5.8.4, Windows 11, Core Ultra 9 275HX, 32 GB RAM, RTX 5070 Laptop GPU. This is a preview, not acceptance of the full second-stage task.

The sections below first record the earlier preview build. Evidence for the subsequent floating UI, mapping, retargeting and offline replay changes is appended at the end; those checks do not constitute full phase acceptance.

## Native build and tests

- Development Editor C++ build passed: `Saved/Logs/BuildPhase2Handoff.log`.
- Existing import/material/menu/enhancement checks plus initial rig checks: 9 tests passed (one expected profile-hash rejection warning), `Saved/Tests/Phase2Automation/index.json`.
- Final native rig tests: 3 passed, `Saved/Tests/NativeRigFinal/index.json`. These exercise compact bone indices, nonidentity bind pose, model yaw conversion, morph weights, mapping replacement, loss recovery, calibration, threshold continuity and profile validation. The synthetic fixture checks CPU pose/weights, not visual equivalence.
- `VRM4U.PMX.RuntimeMorphMetadata`: passed in `-game -NullRHI`, with assertions that `GIsEditor` is false and `GEditor` is null. Evidence: `Saved/Tests/RuntimeMorph/index.json`.
- Six existing character parent materials gained `MorphTargets` usage without rebuilding their graphs. Offline authoring completed with zero errors/warnings: `Saved/Logs/EnableVtuberMaterials.log`.

## Real model and output

March 7th retained 28,039 triangles, 30 materials and no missing base textures. The dynamic import exposes 371 bones and 71 vertex morphs. The default vocabulary maps a subset of the 52 face channels; 33 enabled channels are explicitly reported as missing. This is not support for all PMX morph types.

The stationary GPU run loaded the portable enhanced model and exited normally: `Saved/Logs/Phase2GPUVerified.log`, `Saved/Screenshots/Phase2Static.png`. The development-only pose probe supplies a known head rotation, blink and jaw weights; it is synthetic rig verification, not camera footage. The final probe uses `Phase2PoseVerified.log` / `Phase2PoseVerified.png`.

The independent native Spout receiver received actual 1280×720 BGRA frames. In the first successful read, all 921,600 alpha values were 255. `Saved/Tests/SpoutVerified/received.bmp` preserves raw output. Its original frame-count figure was invalid because the SDK reports every poll as new when shared frame counting is disabled. The corrected probe reports unavailable counters as null; do not treat receiver polls or requested 30 FPS as measured rendered FPS. Final post-AA probe is under `Saved/Tests/SpoutPoseVerified`.

SceneCapture enables persistent TemporalAA/AntiAliasing/PostProcessing show flags; setting them before registration alone was ineffective because UE reset them. Main viewport internal resolution remains 150%; dedicated output capture remains 100%. Transparent output and edge compositing on alternate backgrounds are not accepted.

## Issues found and addressed

The first isolated GPU import terminated in VRM4U: runtime morph metadata registration opened an editor undo transaction through a null `GEditor`. The local fork now creates that metadata without a transaction. A regression test covers this exact non-editor environment. No PMX parsing or geometry reduction was changed.

A late-completing plugin clone overwrote a compiled Spout DLL with the upstream binary. The stale binary was moved into ignored diagnostics, the fixed v2.1.1 pin restored, and the plugin relinked for this engine. Subsequent model/output tests used the rebuilt DLL.

An initial CPU test fixture incorrectly called ActorComponent::Tick on an unregistered test object. The fixture now calls the same private pose-update routine as production after the production Super::Tick call; all final rig tests pass. The initial posed GPU screenshot showed default grey face material because morph shader usage was absent. That authoring setting was fixed before final pose verification.

## Camera and remaining scope

At interactive handoff, a Kaspersky camera-access prompt for HonkaiNativeTracking.exe was visible. The earlier black frames and delayed process exit occurred before that prompt was handled; they cannot be attributed solely to the device driver. The automation surface could not target the protected notification, so the user was asked to allow this specific camera access manually. No global protection was changed. Interactive Esc and Mapping pages were opened and inspected in the standalone HonkaiVTuber window; it remains available for review.

See `NativeTracking.md` for exact native SDK/model hashes, isolation and hardware evidence. Real camera frames reached the offline MediaPipe worker, but the camera image was nearly black, processing was about 1 FPS and no face was detected. Worker shutdown succeeded; the camera broker's device shutdown exceeded six seconds. The preview starts with camera capture off and retains a stopping state while an owned broker is still exiting.

Live person motion direction, usable camera FPS, stable camera reopen, end-to-end latency, hand/body tracking, PNG/PMX scenes, transparent Spout output, sustained runtime and packaged distribution remain unverified or unimplemented. Private models, photos, reference media and generated evidence remain local and ignored by Git. Phase 2 source/plugin changes are uncommitted pending user review.

## Floating UI and rig rework — 2026-11-01

Both project builds passed the semantic automation run recorded at `Saved/Tests/GodotRigAndFloatingUIFinal/index.json` and `../HonkaiRendering/Saved/Tests/GodotRigAndFloatingUIFinal/index.json`: each has 13 successes, one success with an expected incompatible face-profile warning, zero failures and zero tests not run. These are CPU/engine assertions for import, rendering setup, floating UI, mapping and rig behavior; they do not establish visual or live-camera correctness. The light control-style update was made after these runs; its appearance was checked separately in the final GPU screenshot.

The local GPU evidence `Saved/Screenshots/GodotRigReplay.png` and `Saved/Screenshots/FloatingMapping.png` uses the March 7th model. Both 1440 × 900 images have a sampled background corner of RGB (61, 61, 61), matching Blender neutral grey `#3D3D3D`. `FloatingMapping.png` predates the spin-box, slider and checkbox contrast changes. The later `Saved/Screenshots/FloatingMappingFinal.png` shows those controls with readable values and light-theme contrast in an actual GPU viewport; this is a visual check of that page, not proof of live tracking or hair physics.

`GodotRigReplay.png` was driven by the development-only `-VtuberReplay` hook using `Saved/Tests/TrackingReplay/mediapipe_body_hands_current.json`, an official MediaPipe SDK output from a static test image. The fixture has 33 pose landmarks, 33 world pose landmarks, and two hands with 21 image and 21 world landmarks each. The log `Saved/Logs/GodotRigReplay.log` records `VTUBER_REPLAY offline=1 loaded=1` and successful VRM4U model loading. This is neither a real-time camera session nor a recording of the user.

The replay image still shows extreme raised-arm and collar deformation. In this input, the estimated torso angles were about 38.9°, −34.1° and 76.3° before rig safety limits; limiting the angles did not eliminate the visible deformation. Live upper-body/hand tracking, anatomical fidelity and hair gravity/collision remain unverified or incomplete. No camera was accessed in this rework validation.
