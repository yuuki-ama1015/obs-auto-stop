#include <obs-frontend-api.h>
#include <obs-module.h>

#include "auto-stop-dock.hpp"
#include "motion-detector.hpp"
#include "media-end-watcher.hpp"
#include "silence-detector.hpp"
#include "recording-monitor.hpp"
#include "stop-controller.hpp"

#include <QObject>
#include <QTimer>

#include <chrono>
#include <cstdio>
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
// Stop conditions are evaluated on the UI thread (same thread as frontend
// events and the dock), so RecordingMonitor/StopController need no locking.
QTimer *g_eval_timer = nullptr;
constexpr int kEvalIntervalMs = 250;
// Warn once if stillness detection gets no frames for ~10 s.
constexpr int kNoFrameWarnEvals = 10000 / kEvalIntervalMs;
int g_motion_no_frame_evals = 0;
bool g_motion_no_frame_warned = false;
// Set once OBS starts shutting down the frontend; after OBS_FRONTEND_EVENT_EXIT
// the frontend API is gone and must not be called (e.g. from unload).
bool g_shutting_down = false;
bool g_frontend_gone = false;

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

bool anyEnvOverride()
{
	return g_env.max_seconds || g_env.inactivity_seconds ||
	       g_env.min_recording_seconds || g_env.motion;
}

void stopAllDetectors()
{
	g_motion.onRecordingStopped();
	g_media.onRecordingStopped();
	g_silence.onRecordingStopped();
}

void evaluateStopConditions();

void startEvalTimer()
{
	if (g_eval_timer) {
		return;
	}
	g_eval_timer = new QTimer();
	g_eval_timer->setInterval(kEvalIntervalMs);
	g_eval_timer->setTimerType(Qt::CoarseTimer);
	QObject::connect(g_eval_timer, &QTimer::timeout,
			 &evaluateStopConditions);
	g_eval_timer->start();
}

void stopEvalTimer()
{
	if (!g_eval_timer) {
		return;
	}
	g_eval_timer->stop();
	delete g_eval_timer;
	g_eval_timer = nullptr;
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	g_monitor.onFrontendEvent(static_cast<int>(event));

	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		g_stop.clearStopRequested();
		g_motion_no_frame_evals = 0;
		g_motion_no_frame_warned = false;
		g_motion.onRecordingStarted();
		g_media.onRecordingStarted();
		g_silence.onRecordingStarted();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		g_motion.onRecordingPaused();
		g_silence.onRecordingPaused();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		g_motion.onRecordingResumed();
		g_silence.onRecordingResumed();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		stopAllDetectors();
		g_stop.clearStopRequested();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		// Follow the program scene (no-op unless media-end is watching).
		if (!g_shutting_down) {
			g_media.onSceneChanged();
		}
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		// Sources are about to be released: drop our references first.
		g_media.releaseAll();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		if (g_dock) {
			g_dock->flushPendingSave();
		}
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		if (g_dock) {
			g_dock->reloadSettings();
			applyEnvOverrides();
			blog(LOG_INFO, "OBS Auto Stop: settings reloaded for new profile");
		}
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (!g_dock) {
			g_dock = new AutoStopDock(&g_monitor, &g_motion, &g_media,
						  &g_silence, &g_stop);
			// Dock ctor calls loadSettings() and would clobber env;
			// re-apply so OBS_AUTOSTOP_* still wins when set.
			applyEnvOverrides();
			g_dock->setEnvOverrideActive(anyEnvOverride());
			if (obs_frontend_add_dock_by_id("obs-auto-stop-dock",
							"OBS Auto Stop", g_dock)) {
				blog(LOG_INFO, "OBS Auto Stop: dock registered");
			} else {
				blog(LOG_WARNING, "OBS Auto Stop: dock registration failed");
				delete g_dock;
				g_dock = nullptr;
			}
		}
		startEvalTimer();
		break;
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		// Earliest shutdown notification: the frontend API and all sources
		// are still alive. Release everything here, not in module unload.
		g_shutting_down = true;
		stopEvalTimer();
		if (g_dock) {
			g_dock->flushPendingSave();
		}
		stopAllDetectors();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		g_frontend_gone = true;
		stopAllDetectors();
		// The dock is owned (and destroyed) by the OBS main window.
		g_dock = nullptr;
		break;
	default:
		break;
	}
}

void requestStopWithDetails(const char *reason)
{
	char buf[256];
	std::snprintf(buf, sizeof(buf),
		      "%s; elapsed %lld s, still %lld s, silence %lld s, media '%s'",
		      reason,
		      static_cast<long long>(g_monitor.elapsedRecordingTime().count()),
		      static_cast<long long>(g_motion.stillnessDuration().count()),
		      static_cast<long long>(g_silence.silenceActiveDuration().count()),
		      g_media.endedSourceName().c_str());
	g_stop.requestStop(buf);
}

void evaluateStopConditions()
{
	if (!g_monitor.isRecording()) {
		return;
	}
	// Nothing auto-stops while paused (timer and detectors are frozen too).
	if (g_monitor.isPaused()) {
		return;
	}

	// Max-duration timer is always a hard stop when enabled (not part of AND).
	if (g_monitor.isEnabled() && g_monitor.hasReachedMaxDuration()) {
		requestStopWithDetails("max recording duration reached");
		return;
	}

	const auto elapsed = g_monitor.elapsedRecordingTime();

	if (g_motion.isMonitoring() && g_motion.sampleCount() == 0) {
		if (++g_motion_no_frame_evals >= kNoFrameWarnEvals &&
		    !g_motion_no_frame_warned) {
			g_motion_no_frame_warned = true;
			blog(LOG_WARNING,
			     "OBS Auto Stop: stillness detection has received no frames for 10 s");
		}
	} else {
		g_motion_no_frame_evals = 0;
	}

	const bool motion_on = g_motion.isEnabled();
	const bool media_on = g_media.isEnabled();
	const bool silence_on = g_silence.isEnabled();

	const bool motion_hit = motion_on && g_motion.shouldAutoStop(elapsed);
	const bool media_hit = media_on && g_media.shouldAutoStop(elapsed);
	const bool silence_hit = silence_on && g_silence.shouldAutoStop(elapsed);

	const int enabled_count =
		(motion_on ? 1 : 0) + (media_on ? 1 : 0) + (silence_on ? 1 : 0);
	if (enabled_count == 0) {
		return;
	}

	if (g_stop.combineMode() == StopCombineMode::And) {
		if (motion_on && !motion_hit) {
			return;
		}
		if (media_on && !media_hit) {
			return;
		}
		if (silence_on && !silence_hit) {
			return;
		}
		requestStopWithDetails("all enabled conditions met (AND)");
		return;
	}

	// OR (default): any one enabled condition is enough.
	if (motion_hit) {
		requestStopWithDetails("video inactivity duration reached");
		return;
	}
	if (media_hit) {
		requestStopWithDetails("media source ended");
		return;
	}
	if (silence_hit) {
		requestStopWithDetails("audio silence duration reached");
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
	// Clamp to ~10 days so the int / seconds math cannot overflow.
	constexpr long kMaxSeconds = 10L * 24 * 3600;
	*out = static_cast<int>(parsed > kMaxSeconds ? kMaxSeconds : parsed);
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
	return true;
}

void obs_module_unload(void)
{
	stopEvalTimer();
	stopAllDetectors();
	// After OBS_FRONTEND_EVENT_EXIT the frontend API is torn down; calling
	// it here would only log "no callbacks" warnings.
	if (!g_frontend_gone) {
		obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
		if (g_dock) {
			obs_frontend_remove_dock("obs-auto-stop-dock");
			g_dock = nullptr;
		}
	}

	blog(LOG_INFO, "OBS Auto Stop plugin unloaded");
}
