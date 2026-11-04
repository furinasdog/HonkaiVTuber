# Offline enhancement worker

`generate_enhancement.py` runs inside a separate Blender 5.0 process, started with
`--background --factory-startup --python-exit-code 1`. It uses Blender's NumPy and
`bpy`, and imports the existing MMD Tools PMX loader without installing or enabling
an add-on in the user's preferences. No external Python or OpenCV is needed.

```text
blender --background --factory-startup --python-exit-code 1 --python <Scripts/generate_enhancement.py> -- --pmx <absolute.pmx> --output <new-staging-directory> --mmd-tools <directory-containing-mmd_tools> [--material-index N] [--parameters-json <file.json>]
```

The worker accepts a PMX up to 256 MiB. It does not read source texture files or
modify the PMX. Output must be a new staging directory, outside the original
model's directory ancestry. Previously generated profiles are never overwritten.
The optional parameters file accepts only `resolution`: 256, 512 or 1024 (default
512). The parent must include material selection, source resources, generation
parameters and script versions in its cache key.

`status.json` is replaced atomically. Its fields are `version: 1`,
`algorithm: "face-v2"`, `stage`, `progress` (0..1), `state`, and `message`.
Stages are `prepare`, `identify`, `generate`, `validate`, `complete`, `failed`.
State is `running`, `needs_material`, `succeeded`, or `failed`.

Only a single visible material with an exact generic face label is selected
automatically. Ambiguous names or unsuitable automatically selected geometry
produce `needs_material` with `candidates: [{index, name}]`, then normal exit.
The caller should highlight candidates on the model and run a fresh job with
`--material-index`. An explicit invalid selection fails rather than silently
choosing another material. The legacy CLI also accepts an exact `--material` name.

Success writes `profile.json` (the existing version-1 source-SHA1/material schema,
plus algorithm and parameters), `face.png`, `coordinates.png`, and `result.json`.
The result records SHA256 digests of all three deliverable files. `material.json`
is local authoring metadata and is not needed by the runtime. The worker reads
back the generated PNG, verifies channel preservation, and rehashes the PMX
before publishing the successful status. The parent validates the result and
atomically promotes it to the cache; until then, current rendering is unchanged.

Face data is packed, linear RGBA: R/G are side-light thresholds, B controls nose
outline width, A is one minus the small hard nose-shadow mask. Fully transparent
alpha values are data and must not premultiply or erase R/G/B. Older RGB face
maps load with alpha 1, so they retain their previous appearance without a nose
shadow. The generator assumes standard PMX orientation and a separate face
material; unusual topology needs an authored profile.

Cancellation terminates only the caller-owned Blender process tree. The worker
does not promote outputs or delete old caches. An interrupted staging directory
may be removed by the parent once that process has exited. Failure after staging
initialization writes `state: failed` and raises, producing exit code 1 when the
recommended Blender arguments are used. Invalid staging paths are rejected
before any existing files are changed; the parent reports the nonzero exit.

Run `Scripts/test_enhancement.py` in Blender's bundled Python (or a Python with NumPy) for
coordinate monotonicity/mirroring, disconnected UV regions, binary nose-mask
bounds, RGB-profile fallback, ambiguous material selection and input checks.

For end-to-end checks, use a legally obtained local fixture, verify the profile and output hashes, and inspect the generated maps in the host. Keep fixtures and derived images outside version control and distribution packages.
