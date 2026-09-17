# Design

## Scope

OBS Auto Stop is an external plugin that uses the public OBS Plugin API. OBS Studio itself is not forked or modified.

OBS remains responsible for:

```text
video capture
recording
encoding
file storage
```

The plugin will be responsible for:

```text
recording-time monitoring
video-change monitoring
stop-condition evaluation
recording-stop requests
```

## Why not fork OBS Studio?

- Keeps the feature separate from OBS.
- Makes official OBS updates easier to adopt.
- Allows users to remove the plugin and return to stock OBS.
- Simplifies distribution and code ownership.
- Avoids modifying OBS Studio's large codebase.

## Planned responsibilities

- `RecordingMonitor`: recording start/stop state, elapsed time, and maximum-duration checks.
- `MotionDetector`: frame comparison and inactivity measurement.
- `StopController`: one place for combining stop conditions and requesting an OBS recording stop.
- `AutoStopDock`: future OBS Dock UI for configuration and status.
- `plugin-main`: plugin registration, load/unload, and OBS event integration.

The initial implementation intentionally does not add OpenCV, audio analysis, AI-based detection, an installer, or an auto-updater.

## Stop conditions

The intended rule is:

```text
maximum recording time reached OR inactivity duration reached
    -> request OBS recording stop
```

Manual stops must reset timers and inactivity counters. Future safeguards include minimum recording time, sensitivity, monitoring regions, audio silence, black-frame detection, a pre-stop delay, and video-end events.
