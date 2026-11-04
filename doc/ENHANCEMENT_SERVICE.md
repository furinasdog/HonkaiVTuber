# Enhancement service

`FMmdEnhancement` runs `Prepare-MmdEnhancement.ps1` in a separate hidden process. The service polls a small atomic JSON status file; it does not block the game thread while Blender runs. Cancellation terminates only the task's owned process tree. The viewport retains its old materials until a successful output has passed the controller's profile checks.

Data lives under `FPlatformProcess::UserSettingsDir()/<product>`, where `<product>` is `HonkaiRendering` or `HonkaiVTuber` for the corresponding application. `-MmdUserData=<absolute directory>` provides an isolated location for tests. Imports, tools, models, jobs and generated enhancements have separate subdirectories. Active jobs prevent cache clearing. Clearing removes only `Enhancements` and `Jobs`; it refuses directory junctions/reparse points and leaves `Imports`, `Tools` and `Models` intact.

The job request contains `model`, `resource_root`, `data_root`, `scripts`, `material_index`, `parameters` (JSON text) and `force`. `material_index=-1` asks the worker to identify a face; ambiguous geometry returns candidates for user selection. The only generation parameter is `resolution`: 256, 512 (default) or 1024. Keep `-1` in the model settings for automatic selection so subsequent requests use the same identity.

The cache identity combines the complete ordered resource file list and content hashes, material selection, parameters, `face-v2`, and the generation scripts/tool manifest hashes. A successful generation records output hashes and publishes an immutable directory. A small `Index/<key>.json` points to the newest valid version, including after forced regeneration; previous versions remain intact. Model, texture, script or parameter changes cannot restore the old cached profile. Files added during generation also invalidate the job. Corrupt/partial outputs never replace a valid cache.

Model settings are keyed by source PMX content hash and stored through a same-volume atomic Windows rename. Restore rechecks the entire resource file set, scripts, output hashes and parameters. Export creates a new directory containing `<PMX filename>.toon.json` and referenced PNGs. It never overwrites an existing export directory. The controller applies the validated profile to current material instances without rebuilding the mesh or moving the camera.

Pending generation settings and material previews are separate from the applied profile's metadata. A new request commits its parameters only after the output is accepted; cancellation or failure keeps the existing effect and saved configuration. A completed result is consumed once and cannot be replayed after another import.

## Tools and isolation

The launcher first checks installed Blender 5.0.x and MMD Tools 4.5.3. `HONKAI_BLENDER` and `HONKAI_MMD_TOOLS` may explicitly identify an installation; PATH, Windows App Paths, Blender Foundation folders and user extension folders are also checked. Every generation uses `--background --factory-startup`, imports the MMD reader through a temporary Python module path, and does not enable extensions or save user preferences. Generation needs only Blender's bundled Python/NumPy and the MMD Tools module; no system Python installation is required at runtime.

Absent compatible tools, `enhancement-tools.json` pins the official Blender 5.0.0 Windows portable ZIP and official MMD Tools 4.5.3 extension ZIP. Downloads require HTTPS, an approved official host and an exact SHA256 match before extraction. Temporary downloads and extraction stay in the owned job directory; a failed download cannot replace a verified package. ZIP traversal, duplicates, links and oversized packages are rejected.

Pinned checksums were verified against [Blender's 5.0.0 checksum file](https://download.blender.org/release/Blender5.0/blender-5.0.0.sha256) and [MMD Tools' official version history](https://extensions.blender.org/add-ons/mmd-tools/versions/). Blender and MMD Tools retain their GPL licenses; they are separate installed/downloaded tools, not source copied into the MIT viewer. Archive license files remain intact.

## Checks

`python Scripts/test_enhancement_service.py` exercises real Windows/.NET cache key changes, atomic status replacement, safe ZIP extraction and rejected download origins without requiring Blender or network. UE automation `HonkaiRendering.Enhancement.Storage` checks settings persistence, path bounds, portable export and overwrite refusal. The worker also has its separate tests in `test_enhancement.py`.


For integration testing, exercise generation with a compatible local Blender, cache reuse, cancellation, failed validation and fresh publication. Verify GPU application and restart separately; unit tests do not establish those results. Do not redistribute source models or generated maps without permission.
