#pragma once

#include <chrono>

// Tracks recording start/stop and elapsed time; decides when max duration is reached.
class RecordingMonitor {
public:
	using clock = std::chrono::steady_clock;
	using seconds = std::chrono::seconds;

	RecordingMonitor() = default;

	void setEnabled(bool enabled);
	bool isEnabled() const { return enabled_; }

	// Maximum recording duration. Values <= 0 disable the max-time stop condition.
	void setMaxRecordingDuration(seconds duration);
	seconds maxRecordingDuration() const { return max_duration_; }

	bool isRecording() const { return recording_; }
	seconds elapsedRecordingTime() const;
	bool hasReachedMaxDuration() const;

	// Called from obs_frontend_event_cb.
	void onFrontendEvent(int event);

	// Called from obs tick callback while recording is active.
	void tick();

	// Reset timers/counters (e.g. after a manual stop).
	void reset();

private:
	void onRecordingStarted();
	void onRecordingStopped();
	void accumulatePauseIfNeeded(clock::time_point now);

	bool enabled_ = true;
	seconds max_duration_{120 * 60}; // default 120 minutes

	bool recording_ = false;
	bool paused_ = false;

	clock::time_point segment_started_{};
	clock::time_point pause_started_{};
	std::chrono::milliseconds active_elapsed_{0};
	std::chrono::milliseconds paused_total_{0};
};
