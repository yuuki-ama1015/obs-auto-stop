#include <obs-frontend-api.h>
#include <obs-module.h>

#include "auto-stop-dock.hpp"
#include "motion-detector.hpp"
#include "recording-monitor.hpp"
#include "stop-controller.hpp"

#include <chrono>
#include <cstdlib>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-auto-stop", "en-US")

namespace {

RecordingMonitor g_monitor;
MotionDetector g_motion;
StopController g_stop;
AutoStopDock *g_dock = nullptr;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	g_monitor.onFrontendEvent(static_cast<int>(event));

	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		g_motion.onRecordingStarted();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		g_motion.onRecordingStopped();
		g_stop.clearStopRequested();
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (!g_dock) {
			g_dock = new AutoStopDock(&g_monitor, &g_motion, &g_stop);
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

	if (g_motion.shouldAutoStop(g_monitor.elapsedRecordingTime())) {
		g_stop.requestStop("video inactivity duration reached");
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
		g_monitor.setMaxRecordingDuration(
			std::chrono::seconds{override_seconds});
		blog(LOG_INFO,
		     "OBS Auto Stop: max recording duration overridden to %d s (OBS_AUTOSTOP_MAX_SECONDS)",
		     override_seconds);
	}

	if (envLong("OBS_AUTOSTOP_INACTIVITY_SECONDS", &override_seconds) ==
	    0) {
		g_motion.setInactivityDuration(
			std::chrono::seconds{override_seconds});
		blog(LOG_INFO,
		     "OBS Auto Stop: inactivity duration overridden to %d s (OBS_AUTOSTOP_INACTIVITY_SECONDS)",
		     override_seconds);
	}

	if (envLong("OBS_AUTOSTOP_MIN_RECORDING_SECONDS", &override_seconds) ==
	    0) {
		g_motion.setMinimumRecordingDuration(
			std::chrono::seconds{override_seconds});
		blog(LOG_INFO,
		     "OBS Auto Stop: min recording duration overridden to %d s",
		     override_seconds);
	}

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_add_tick_callback(onTick, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_remove_tick_callback(onTick, nullptr);
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	g_motion.onRecordingStopped();

	if (g_dock) {
		obs_frontend_remove_dock("obs-auto-stop-dock");
		g_dock = nullptr;
	}

	blog(LOG_INFO, "OBS Auto Stop plugin unloaded");
}
