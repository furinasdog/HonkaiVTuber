# HonkaiVTuber preview

Independent derivative of HonkaiRendering, starting at `8ba15b0`. The C++ module and `.uproject` keep their original internal names to preserve asset references; the visible application is HonkaiVTuber. Phase 2 changes are local and await user review before commit/push.

## Run

Use UE 5.8, initialize the pinned submodules, build the `HonkaiRenderingEditor` Win64 Development target, and run `Scripts/Start-VtuberPreview.ps1`. The native tracker is prepared separately by `Scripts/Prepare-NativeTracking.ps1`. Existing Unreal, Visual Studio, Blender and the installed official MediaPipe native DLL are reused when compatible.

Drag a PMX ZIP into the application and choose the character. Startup shows only the character on Blender's neutral grey background. H or Esc reveals draggable, collapsible six-entry navigation in the upper left; its draggable pages stay inside the viewport. H hides or restores all UI; Esc closes an open page, then toggles the navigation. Background camera controls work while an ordinary page is open; PMX selection and loading status remain modal. The Tracking page starts/stops the camera and reports fresh face, upper-body and hand detection. Mapping has Face, Arms & Hands, and Options tabs. It edits original PMX vertex morph and bone targets, presents three combined head axes and four left/right arm/hand groups, and retains advanced per-bone editing. Group settings write to all related joints. Enable/invert/smoothing/sensitivity/curvature/precision/threshold controls drive the live response curve. Neutral calibration is saved with the model-specific profile after a 0.35-second idle period or when closed. An unmapped channel remains visible but its response controls are disabled. The Spout page under Graphics requests the `HonkaiVTuber` sender at 1280 × 720 / 30 FPS. WASD moves the character relative to camera heading, Shift moves up, and Space moves down. Horizontal middle-mouse orbit follows the drag direction; Shift + middle mouse pans and Ctrl + middle mouse or the wheel zooms.

The rig recognizes generic bone and expression vocabulary. Missing channels stay visible, and users can select a valid target before editing the response. PMX group, bone and material morphs are not converted to vertex morphs. Model position is separate from tracked head rotation. Face lighting uses the animated head rotation relative to its bind pose; the outline copies bone and morph state through VRM4U.

## Current preview boundary

The native parser and rig now accept pose and two-hand landmarks, and a development-only offline replay can exercise these channels with a static official SDK fixture. Live upper-body/hand performance and anatomy still require visual verification with a working camera. PNG/PMX scene composition, transparent Spout output and packaged distribution are not part of this runnable preview. Opaque output explicitly writes alpha 1; transparent edges are not claimed as verified. A native receiver probe is provided in `Tools/SpoutProbe` to measure actual frames without changing OBS scenes.

In a development Editor build, `-VtuberReplay=<absolute JSON path>` reads one native-tracker JSON frame and resubmits it with a fresh timestamp each tick. It logs `VTUBER_REPLAY offline=1` and skips automatic camera startup even when `-VtuberCamera` is also present. This is a static pose test, not a recording of a user session.

For a GPU UI check with `-MmdCapture=<absolute PNG path>`, add `-MmdCapturePage=Mapping`, `Capture`, or `Nav`. The selected in-viewport page opens six seconds after model load, and the existing screenshot request runs at eight seconds. `Capture` selects the Tracking page; `Nav` shows only the floating navigation. No camera is started by this page-selection hook.

Imported models and enhancement data are stored under the separate HonkaiVTuber user-data directory. Session data and mapping profiles remain under ignored `Saved/`. Models, reference media, tool binaries and generated outputs are excluded from Git. See `THIRD_PARTY_NOTICES.md` and the native tracking documentation for dependency licenses and local inference isolation.
