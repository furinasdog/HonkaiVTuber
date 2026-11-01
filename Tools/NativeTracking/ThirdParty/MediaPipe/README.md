# MediaPipe C API headers

The unmodified files in `include/mediapipe/tasks/c` were copied from
[google-ai-edge/mediapipe v0.10.35](https://github.com/google-ai-edge/mediapipe/tree/v0.10.35/mediapipe/tasks/c).
They retain the upstream Apache 2.0 copyright notices; the complete license is in `LICENSE`.
These headers are intentionally pinned to the same version as the native binary. Newer C APIs have renamed structs and are not assumed ABI compatible.

The native Windows DLL is extracted from the official `mediapipe-0.10.35-py3-none-win_amd64.whl` or reused from a matching local installation after SHA256 verification. The wheel is only a distribution container; no Python process or runtime is needed by the helper.

Pinned artifacts:

| Artifact | SHA256 |
| --- | --- |
| Windows x64 wheel | `b08f001cf3c3cd0d88d9ed68f3368dc8a4913f568281a93117f083115aa672ba` |
| `libmediapipe.dll` | `aa8e6c1b618c30cd3a6ad584dee1b2f2c99c3f3025d683bada36e1566d9092b7` |
| FaceLandmarker float16 model version 1 | `64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff` |
| HandLandmarker float16 model version 1 | `fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1` |
| PoseLandmarker Lite float16 model version 1 | `59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a` |

Binary and models are downloaded by `Scripts/Prepare-NativeTracking.ps1` into ignored directories, not committed.

The official prebuilt SDK includes usage telemetry. The project therefore runs it in a native Windows AppContainer with no network capability. See [upstream issue 6291](https://github.com/google-ai-edge/mediapipe/issues/6291) and [MediaPipe API terms](https://developers.google.com/edge/mediapipe/legal/tos). The MIT license of HonkaiVTuber does not replace upstream licensing or model/API terms.
