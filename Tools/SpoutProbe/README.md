# Spout receiver probe

This Windows C++ diagnostic reuses the pinned Spout SDK in `Plugins/Spout2_DX12`.
It connects only to `HonkaiVTuber`; it does not open OBS, edit scenes, select the global active sender, start streaming, or access a camera.

Build with `Build.cmd` from a terminal. The script uses an existing Visual Studio x64 toolchain and copies the existing SDK DLLs beside the executable.

After the UE project has enabled Spout output, run from the project root:

```powershell
./Tools/SpoutProbe/bin/HonkaiSpoutProbe.exe --seconds 10 --out ./Saved/Tests/SpoutProbe
```

The receive loop is bounded to 1–10 seconds and uses a nonblocking staging-texture map. `receiver.json` records dimensions, successful receive polls, raw SDK flags, positive sender frame-counter changes, alpha statistics and the `opaque_verified` result. Frame counts/rates are `null` unless an advancing positive sender counter is observed. Spout's `IsFrameNew()` can remain true when frame counting is disabled; receive poll frequency is not sender frame rate. `received.bmp` preserves the received BGRA pixels and alpha as a top-down 32-bit BMP. Dimensions and format must remain fixed during a run.

The first snapshot is taken after at least three successful receive polls and 0.5 seconds. `opaque_verified=true` requires a successful readback and every snapshot alpha byte to be 255. This does not validate transparent output, sender frame rate, or visual similarity to the main viewport. The operating system graphics driver can still delay device creation/teardown beyond the polling deadline. `--self-test` checks zero/duplicate/jumping/reset frame counters without opening a graphics device.

The diagnostic code is covered by the project's MIT license. The reused Spout SDK retains the BSD-2-Clause notice in its headers and the project's third-party notices.
