# Build

## Prerequisites

- CMake 3.23 or newer
- Ninja
- X-Plane SDK
- MinGW-w64 for the productive Windows build

Set `XPLANE_SDK_PATH` to the SDK root containing `CHeaders`.

The bundled presets keep build directories outside the source repository under:

```text
../YAL-Auto-Unicom-Helper-build/
```

## Native tests

```bash
cmake --preset build-mac
cmake --build --preset build-mac
ctest --test-dir ../YAL-Auto-Unicom-Helper-build/build-mac --output-on-failure
```

The macOS plugin build is a compile check only. Productive UIA, SAPI, WASAPI
and PTT support are Windows-only.

## Windows build

```bash
XPLANE_SDK_PATH="$XPLANE_SDK_PATH" ./scripts/build-win-mingw.sh
```

The script stages:

```text
deploy/YAL_AutoUnicomHelper/64/win.xpl
deploy/YAL_AutoUnicomHelper/resources/auto_unicom_chime.wav
```

Cross-compiled Windows tests cannot execute on macOS; run the native tests
first. The same portable logic is compiled again into the Windows target.
