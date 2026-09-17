#pragma once

// Centralizes OBS recording-stop requests so call sites stay in one place.
class StopController {
public:
	StopController() = default;

	// Ask OBS to stop the current recording. Safe to call when not recording.
	void requestStop(const char *reason);

	bool stopRequested() const { return stop_requested_; }
	void clearStopRequested() { stop_requested_ = false; }

private:
	bool stop_requested_ = false;
};
