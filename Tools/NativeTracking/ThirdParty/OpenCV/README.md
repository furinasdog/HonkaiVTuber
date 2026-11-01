# Existing OpenCV C++ SDK

The camera broker links against the already installed UE5.8 OpenCV 4.5.5 SDK.
OpenCV is Apache 2.0 licensed; the notices shipped with the installed SDK are retained in this directory.
The executable delay-loads `opencv_world455.dll` so the inference-only AppContainer worker does not load the camera SDK.

Validated local DLL SHA256: `718642ca44e0eb01a58693d23009954c27455abc379d870b5f52b165e2c1942a`.
`Prepare-NativeTracking.ps1` copies the existing local SDK DLL into the ignored binary directory; it does not redistribute the UE installation in Git or replace its license terms.

Upstream: https://github.com/opencv/opencv/tree/4.5.5

The primary capture backend is `CAP_DSHOW`, selected after this machine's Media Foundation device activation failed or stalled. Requested camera FPS is reported separately from measured frame processing FPS. A camera accepting a 30 FPS property does not prove it supplies 30 FPS.
