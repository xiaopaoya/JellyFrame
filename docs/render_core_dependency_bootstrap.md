# Render Core Dependency Bootstrap

> Last updated: 2026-11-14; Applies to: 0.6.0-dev; Runtime 0.8.0 / Core ABI 3 lock

`tools/bootstrap_render_core.py` builds and installs the exact Render Core
version, ABI and source identity in `cmake/jellyframe_dependency_lock.cmake`.
It works offline after the source archive and its trusted SHA-256 are available.
It does not download dependencies, modify the lock or change Runtime's default
`in-tree` provider.

Start with a new workspace path. The tool checks the archive checksum before
creating output, extracts only regular files and directories below the expected
versioned archive root, and enforces member-count and unpacked-size limits. After
installation, it checks the CMake package identity, source manifest and feature
profile against one another and against the Runtime lock, then writes
`bootstrap-result.json`.

The current lock accepts Core `0.8.0` / ABI `3`, with source manifest identity
`fa5914ce1559da5b12522fa28775287a535543d1be64da19a985e3d3f84cd795`. The
published archive SHA-256 is
`b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240`.
Use the checksum published for the reviewed release artifact; do not trust an
unverified adjacent sidecar as the authority.

```powershell
python tools\bootstrap_render_core.py `
  --archive C:\dependencies\jellyframe-render-core-0.8.0.tar.gz `
  --expected-sha256 b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240 `
  --workspace C:\build\render-core-0.8.0 `
  --generator "Visual Studio 17 2022" `
  --architecture x64
```

Then select the installed package explicitly for a Runtime boundary build:

```powershell
cmake -S . -B build\runtime-core-package `
  -DJELLYFRAME_RENDER_CORE_PROVIDER=package `
  -DJELLYFRAME_RENDER_CORE_PACKAGE_DIR=C:\build\render-core-0.8.0\install `
  -DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF
```

The archive checksum identifies the exact distribution bytes; the source
manifest hash identifies its normalized source set. Both checks matter. A local
unreleased archive can support development qualification but cannot replace a
signed, reviewed release artifact.
