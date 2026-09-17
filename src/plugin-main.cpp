#include <obs-frontend-api.h>
#include <obs-module.h>

#include "recording-monitor.hpp"
#include "stop-controller.hpp"

#include <chrono>
#include <cstdlib>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-auto-stop", "en-US")

namespace {

RecordingMonitor g_monitor;
StopController g_stop;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	g_monitor.onFrontendEvent(static_cast<int>(event));

	// Manual or external stop: clear stop latch when recording ends.
	if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		g_stop.clearStopRequested();
	}
}

void onTick(void *, float)
{
	if (!g_monitor.isRecording()) {
		return;
	}

	if (g_monitor.hasReachedMaxDuration()) {
		g_stop.requestStop("max recording duration reached");
	}
}

int maxDurationSecondsFromEnv()
{
	const char *value = std::getenv("OBS_AUTOSTOP_MAX_SECONDS");
	if (!value || !*value) {
		return -1;
	}
	char *end = nullptr;
	long parsed = std::strtol(value, &end, 10);
	if (end == value || parsed < 0) {
		return -1;
	}
	return static_cast<int>(parsed);
}

} // namespace

bool obs_module_load(void)
{
	blog(LOG_INFO, "OBS Auto Stop plugin loaded");

	const int env_max = maxDurationSecondsFromEnv();
	if (env_max >= 0) {
		g_monitor.setMaxRecordingDuration(
			std::chrono::seconds{env_max});
		blog(LOG_INFO,
		     "OBS Auto Stop: max recording duration overridden to %d s (OBS_AUTOSTOP_MAX_SECONDS)",
		     env_max);
	}

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_add_tick_callback(onTick, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_remove_tick_callback(onTick, nullptr);
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	blog(LOG_INFO, "OBS Auto Stop plugin unloaded");
}
