#include <obs-frontend-api.h>
#include <obs-module.h>

#include "auto-stop-dock.hpp"
#include "motion-detector.hpp"
#include "media-end-watcher.hpp"
#include "silence-detector.hpp"
#include "recording-monitor.hpp"
#include "stop-controller.hpp"

#include <chrono>
#include <cstdlib>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-auto-stop", "en-US")

namespace {

RecordingMonitor g_monitor;
MotionDetector g_motion;
MediaEndWatcher g_media;
SilenceDetector g_silence;
StopController g_stop;
AutoStopDock *g_dock = nullptr;

// Remember OBS_AUTOSTOP_* env overrides so they can win over profile
// loadSettings() which runs later when the dock is created.
struct EnvOverrides {
	bool max_seconds = false;
	int max_seconds_val = 0;
	bool inactivity_seconds = false;
	int inactivity_seconds_val = 0;
	bool min_recording_seconds = false;
	int min_recording_seconds_val = 0;
	bool motion = false;
	bool motion_enabled = false;
} g_env;

void applyEnvOverrides()
{
	// Re-apply after AutoStopDock::loadSettings() so env beats profile.
	if (g_env.max_seconds) {
		g_monitor.setMaxRecordingDuration(
			std::chrono::seconds{g_env.max_seconds_val});
	}
	if (g_env.inactivity_seconds) {
		g_motion.setInactivityDuration(
			std::chrono::seconds{g_env.inactivity_seconds_val});
	}
	if (g_env.min_recording_seconds) {
		const auto dur =
			std::chrono::seconds{g_env.min_recording_seconds_val};
		g_motion.setMinimumRecordingDuration(dur);
		g_media.setMinimumRecordingDuration(dur);
		g_silence.setMinimumRecordingDuration(dur);
	}
	if (g_env.motion) {
		g_motion.setEnabled(g_env.motion_enabled);
	}
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	g_monitor.onFrontendEvent(static_cast<int>(event));

	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		g_motion.onRecordingStarted();
		g_media.onRecordingStarted();
		g_silence.onRecordingStarted();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		g_motion.onRecordingStopped();
		g_media.onRecordingStopped();
		g_silence.onRecordingStopped();
		g_stop.clearStopRequested();
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (!g_dock) {
			g_dock = new AutoStopDock(&g_monitor, &g_motion, &g_media,
						  &g_silence, &g_stop);
			// Dock ctor calls loadSettings() and would clobber env;
			// re-apply so OBS_AUTOSTOP_* still wins when set.
			applyEnvOverrides();
			obs_frontend_add_dock_by_id("obs-auto-stop-dock",
						    "OBS Auto Stop", g_dock);
			blog(LOG_INFO, "OBS Auto Stop: dock registered");
		}
		break;
	default:
		break;
	}
}

void onTick(void *, float)
{
	if (!g_monitor.isRecording()) {
		return;
	}

	if (g_monitor.isEnabled() && g_monitor.hasReachedMaxDuration()) {
		g_stop.requestStop("max recording duration reached");
		return;
	}

	const auto elapsed = g_monitor.elapsedRecordingTime();

	if (g_motion.shouldAutoStop(elapsed)) {
		g_stop.requestStop("video inactivity duration reached");
		return;
	}

	if (g_media.shouldAutoStop(elapsed)) {
		g_stop.requestStop("media source ended");
		return;
	}

	if (g_silence.shouldAutoStop(elapsed)) {
		g_stop.requestStop("audio silence duration reached");
	}
}

int envLong(const char *name, int *out)
{
	const char *value = std::getenv(name);
	if (!value || !*value) {
		return -1;
	}
	char *end = nullptr;
	long parsed = std::strtol(value, &end, 10);
	if (end == value || parsed < 0) {
		return -1;
	}
	*out = static_cast<int>(parsed);
	return 0;
}

} // namespace

bool obs_module_load(void)
{
	blog(LOG_INFO, "OBS Auto Stop plugin loaded");

	int override_seconds = 0;
	if (envLong("OBS_AUTOSTOP_MAX_SECONDS", &override_seconds) == 0) {
		g_env.max_seconds = true;
		g_env.max_seconds_val = override_seconds;
		blog(LOG_INFO,
		     "OBS Auto Stop: max recording duration overridden to %d s (OBS_AUTOSTOP_MAX_SECONDS)",
		     override_seconds);
	}

	if (envLong("OBS_AUTOSTOP_INACTIVITY_SECONDS", &override_seconds) ==
	    0) {
		g_env.inactivity_seconds = true;
		g_env.inactivity_seconds_val = override_seconds;
		blog(LOG_INFO,
		     "OBS Auto Stop: inactivity duration overridden to %d s (OBS_AUTOSTOP_INACTIVITY_SECONDS)",
		     override_seconds);
	}

	if (envLong("OBS_AUTOSTOP_MIN_RECORDING_SECONDS", &override_seconds) ==
	    0) {
		g_env.min_recording_seconds = true;
		g_env.min_recording_seconds_val = override_seconds;
		blog(LOG_INFO,
		     "OBS Auto Stop: min recording duration overridden to %d s",
		     override_seconds);
	}

	if (const char *motion_env = std::getenv("OBS_AUTOSTOP_MOTION")) {
		const bool on = motion_env[0] == '1' || motion_env[0] == 't' ||
				motion_env[0] == 'T' || motion_env[0] == 'y' ||
				motion_env[0] == 'Y';
		g_env.motion = true;
		g_env.motion_enabled = on;
		blog(LOG_INFO, "OBS Auto Stop: motion detection %s (OBS_AUTOSTOP_MOTION)",
		     on ? "enabled" : "disabled");
	}

	applyEnvOverrides();

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_add_tick_callback(onTick, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_remove_tick_callback(onTick, nullptr);
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	g_motion.onRecordingStopped();
	g_media.onRecordingStopped();
	g_silence.onRecordingStopped();

	if (g_dock) {
		obs_frontend_remove_dock("obs-auto-stop-dock");
		g_dock = nullptr;
	}

	blog(LOG_INFO, "OBS Auto Stop plugin unloaded");
}
