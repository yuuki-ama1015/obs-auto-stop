# OBS Auto Stop

OBS Studio plugin for automatically stopping recordings based on timers and video inactivity detection.

Status: Early development / experimental

This project is an external OBS plugin. It does not modify the OBS Studio source tree.

## Planned features

- Configurable recording timer
- Configurable inactivity duration
- Motion detection
- OBS Dock UI
- Minimum recording duration
- Motion sensitivity
- Optional audio inactivity detection
- Safe manual-stop handling

## Build

The project does not contain the OBS SDK. Supply paths through CMake configuration or environment variables:

```powershell
cmake -S . -B build `
  -DOBS_SOURCE_DIR=C:/path/to/obs-studio `
  -DOBS_BUILD_DIR=C:/path/to/obs-studio/build
cmake --build build --config RelWithDebInfo
```

If the OBS SDK is not found, CMake configures successfully and reports that the plugin target was skipped.

## Development status

Milestone 0.1 contains only the load/unload scaffold. The plugin is not ready for production use, and OBS loading has not yet been verified in this repository.

No `LICENSE` file is included yet. License compatibility with OBS Studio and future dependencies must be reviewed before selecting one.
