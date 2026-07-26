# Milah

Milah is an offline manuscript-comparison editor. It loads read-only OSIS
witnesses and optional translations, aligns their readings, generates a
reviewable Combined edition, and saves a portable `.milah` project.

Milah is deliberately independent of every tool outside this directory. It
does not import PDFs and does not use the repository's PDF-to-OSIS converter.

## Current implementation

- C++20/Qt 6 desktop shell with `QWebEngineView` and `QWebChannel`.
- React/TypeScript interface that can later be hosted in a browser.
- Namespace-aware OSIS import with safe DTD/entity rejection.
- Common-book/chapter detection, RTL token grids, note tooltips, manuscript
  alignment, majority consensus, priority-witness tie handling, and Combined
  editing.
- Optional translation association and correctable alignment spans.
- Atomic portable-project storage using ZIP, plus standalone Combined OSIS
  export.

The current workstation does not have the required build toolchain. The files
were therefore authored and reviewed statically; no dependency installation,
configuration, compilation, or tests were run here.

## Requirements for a future build machine

- Windows 10 or 11.
- Visual Studio 2022 with the Desktop development with C++ workload.
- CMake 3.24 or newer.
- Qt 6.6 or newer with:
  - Qt Core
  - Qt Gui
  - Qt Widgets
  - Qt WebChannel
  - Qt WebEngine
- Node.js 20 or newer and npm.
- QuaZip 1.4 or newer built for the same Qt 6 toolchain and discoverable by
  CMake as `QuaZip-Qt6`.

Node.js is needed only to create the embedded frontend bundle. Python is not
needed to build or run Milah.

## Future build

Run these commands only on a machine with the requirements above:

```powershell
cd app\frontend
npm install
cd ..
cmake -S . -B build `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64;C:\path\to\quazip"
cmake --build build --config Release
```

CMake invokes the frontend production build and embeds its fixed-name output
in the Qt resource system. To run `windeployqt` after building:

```powershell
cmake -S . -B build `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64;C:\path\to\quazip" `
  -DMILAH_DEPLOY_QT=ON
cmake --build build --config Release
```

The resulting desktop executable is `Milah.exe`.

## Minimal future tests

Tests are intentionally limited to three small cases:

- `frontend/tests/osis.test.ts`
- `frontend/tests/consensus.test.ts`
- `tests/project_storage_test.cpp`

On a configured build machine:

```powershell
cd app\frontend
npm test
cd ..
cmake -S . -B build-tests `
  -DMILAH_BUILD_TESTS=ON `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64;C:\path\to\quazip"
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```

## Project format

A `.milah` file is a versioned ZIP archive. It contains `manifest.json`,
embedded source OSIS files, the Combined state, translation associations and
alignment corrections, review state, and a current Combined OSIS snapshot.
Paths inside archives are validated before reading or writing.

Source manuscript and translation text is immutable. Only Combined text and
translation alignment metadata can be changed.
