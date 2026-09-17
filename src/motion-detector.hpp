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

	// Monitor only a percentage region of the analyzed frame.
	// When disabled, the full frame is used. Values are clamped to 0-100.
	void setRegionEnabled(bool enabled);
	bool isRegionEnabled() const { return region_enabled_; }
	void setRegionPercent(int x, int y, int w, int h);
	void regionPercent(int &x, int &y, int &w, int &h) const;

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
	void clearPreviousFrame();
	void clampRegion();
	void resolveRegionPixels(uint32_t frameW, uint32_t frameH, uint32_t &rx,
				 uint32_t &ry, uint32_t &rw, uint32_t &rh) const;
	static double meanAbsDiffPercent(const uint8_t *a, const uint8_t *b,
					 size_t count);

	bool enabled_ = false;
	seconds inactivity_duration_{15};
	double sensitivity_percent_{2.0};
	seconds min_recording_duration_{5 * 60};

	bool region_enabled_ = false;
	int region_x_percent_ = 0;
	int region_y_percent_ = 0;
	int region_w_percent_ = 100;
	int region_h_percent_ = 100;

	bool monitoring_ = false;
	bool callback_registered_ = false;

	std::vector<uint8_t> prev_luma_;
	uint32_t analyze_width_ = 0;
	uint32_t analyze_height_ = 0;
	uint32_t region_pixel_w_ = 0;
	uint32_t region_pixel_h_ = 0;

	double last_motion_percent_ = 0.0;
	clock::time_point stillness_started_{};
	bool stillness_active_ = false;
};
