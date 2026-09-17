#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

struct video_data;
struct video_scale_info;

// Detects video inactivity by comparing downscaled frames.
// Does not call OBS stop APIs; exposes shouldAutoStop() for StopController wiring.
class MotionDetector {
public:
	using clock = std::chrono::steady_clock;
	using seconds = std::chrono::seconds;

	MotionDetector() = default;
	~MotionDetector();

	MotionDetector(const MotionDetector &) = delete;
	MotionDetector &operator=(const MotionDetector &) = delete;

	void setEnabled(bool enabled);
	bool isEnabled() const { return enabled_; }

	void setInactivityDuration(seconds duration);
	seconds inactivityDuration() const { return inactivity_duration_; }

	// Sensitivity as a percentage of full-scale luma difference (e.g. 2.0 = 2%).
	void setSensitivityPercent(double percent);
	double sensitivityPercent() const { return sensitivity_percent_; }

	// Ignore stillness until the recording has lasted at least this long.
	void setMinimumRecordingDuration(seconds duration);
	seconds minimumRecordingDuration() const { return min_recording_duration_; }

	bool isMonitoring() const { return monitoring_; }
	double lastMotionPercent() const { return last_motion_percent_; }
	seconds stillnessDuration() const;

	bool shouldAutoStop(seconds recordingElapsed) const;

	void onRecordingStarted();
	void onRecordingStopped();

	// Raw-video callback target (registered via obs_add_raw_video_callback).
	void onVideoFrame(const struct video_data *frame);

	static struct video_scale_info makeConversion();

private:
	void ensureCallbackRegistered();
	void ensureCallbackRemoved();
	void resetStillness();
	static double meanAbsDiffPercent(const uint8_t *a, const uint8_t *b,
					 size_t count);

	bool enabled_ = false;
	seconds inactivity_duration_{15};
	double sensitivity_percent_{2.0};
	seconds min_recording_duration_{5 * 60};

	bool monitoring_ = false;
	bool callback_registered_ = false;

	std::vector<uint8_t> prev_luma_;
	uint32_t frame_width_ = 0;
	uint32_t frame_height_ = 0;

	double last_motion_percent_ = 0.0;
	clock::time_point stillness_started_{};
	bool stillness_active_ = false;
};
