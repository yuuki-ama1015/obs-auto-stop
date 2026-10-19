#include "recording-monitor.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <algorithm>

void RecordingMonitor::setEnabled(bool enabled)
{
	// Elapsed state is kept; hasReachedMaxDuration() checks enabled_.
	enabled_ = enabled;
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
			// Freeze the active segment; pause time is never counted.
			active_elapsed_ += std::chrono::duration_cast<std::chrono::milliseconds>(
				clock::now() - segment_started_);
			paused_ = true;
		}
		break;
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		if (recording_ && paused_) {
			// Resume the active segment clock.
			segment_started_ = clock::now();
			paused_ = false;
		}
		break;
	default:
		break;
	}
}

void RecordingMonitor::reset()
{
	recording_ = false;
	paused_ = false;
	active_elapsed_ = std::chrono::milliseconds{0};
	segment_started_ = clock::time_point{};
}

void RecordingMonitor::onRecordingStarted()
{
	// Set the clock before flagging recording_ so elapsed is never computed
	// against a default (epoch) time point.
	active_elapsed_ = std::chrono::milliseconds{0};
	segment_started_ = clock::now();
	paused_ = false;
	recording_ = true;
	blog(LOG_INFO, "OBS Auto Stop: recording started (max %lld s)",
	     static_cast<long long>(max_duration_.count()));
}

void RecordingMonitor::onRecordingStopped()
{
	auto now = clock::now();
	if (recording_ && !paused_) {
		active_elapsed_ +=
			std::chrono::duration_cast<std::chrono::milliseconds>(now - segment_started_);
	}

	// Freeze state before reading elapsed so we do not double-count the open segment.
	recording_ = false;
	paused_ = false;
	blog(LOG_INFO, "OBS Auto Stop: recording stopped (elapsed %lld s)",
	     static_cast<long long>(elapsedRecordingTime().count()));
	reset();
}
