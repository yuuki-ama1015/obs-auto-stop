#include "recording-monitor.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <algorithm>

void RecordingMonitor::setEnabled(bool enabled)
{
	enabled_ = enabled;
	if (!enabled_) {
		// Keep elapsed state, but do not auto-stop while disabled.
	}
}

void RecordingMonitor::setMaxRecordingDuration(seconds duration)
{
	if (duration < seconds{0}) {
		duration = seconds{0};
	}
	max_duration_ = duration;
}

RecordingMonitor::seconds RecordingMonitor::elapsedRecordingTime() const
{
	if (!recording_) {
		return std::chrono::duration_cast<seconds>(active_elapsed_);
	}

	auto now = clock::now();
	auto elapsed = active_elapsed_;
	if (!paused_) {
		elapsed += std::chrono::duration_cast<std::chrono::milliseconds>(now - segment_started_);
	}
	return std::chrono::duration_cast<seconds>(elapsed);
}

bool RecordingMonitor::hasReachedMaxDuration() const
{
	if (!enabled_ || !recording_) {
		return false;
	}
	if (max_duration_.count() <= 0) {
		return false;
	}
	return elapsedRecordingTime() >= max_duration_;
}

void RecordingMonitor::onFrontendEvent(int event)
{
	switch (static_cast<enum obs_frontend_event>(event)) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		onRecordingStarted();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		onRecordingStopped();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		if (recording_ && !paused_) {
			pause_started_ = clock::now();
			paused_ = true;
		}
		break;
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		if (recording_ && paused_) {
			accumulatePauseIfNeeded(clock::now());
			// Resume the active segment clock.
			segment_started_ = clock::now();
			paused_ = false;
		}
		break;
	default:
		break;
	}
}

void RecordingMonitor::tick()
{
	if (!recording_ || paused_) {
		return;
	}
	// Elapsed is computed on demand from segment_started_; nothing else required per tick.
}

void RecordingMonitor::reset()
{
	recording_ = false;
	paused_ = false;
	active_elapsed_ = std::chrono::milliseconds{0};
	paused_total_ = std::chrono::milliseconds{0};
	segment_started_ = clock::time_point{};
	pause_started_ = clock::time_point{};
}

void RecordingMonitor::onRecordingStarted()
{
	recording_ = true;
	paused_ = false;
	active_elapsed_ = std::chrono::milliseconds{0};
	paused_total_ = std::chrono::milliseconds{0};
	segment_started_ = clock::now();
	blog(LOG_INFO, "OBS Auto Stop: recording started (max %lld s)",
	     static_cast<long long>(max_duration_.count()));
}

void RecordingMonitor::onRecordingStopped()
{
	auto now = clock::now();
	if (recording_ && !paused_) {
		active_elapsed_ +=
			std::chrono::duration_cast<std::chrono::milliseconds>(now - segment_started_);
	} else if (recording_ && paused_) {
		accumulatePauseIfNeeded(now);
	}

	// Freeze state before reading elapsed so we do not double-count the open segment.
	recording_ = false;
	paused_ = false;
	blog(LOG_INFO, "OBS Auto Stop: recording stopped (elapsed %lld s)",
	     static_cast<long long>(elapsedRecordingTime().count()));
	reset();
}

void RecordingMonitor::accumulatePauseIfNeeded(clock::time_point now)
{
	if (!paused_) {
		return;
	}
	paused_total_ += std::chrono::duration_cast<std::chrono::milliseconds>(now - pause_started_);
	// Pause time is excluded from active_elapsed_ by not advancing segment during pause.
	(void)paused_total_;
}
