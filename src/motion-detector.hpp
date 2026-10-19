#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

struct gs_texture_render;
struct gs_stage_surface;

// Detects video inactivity by comparing small downscaled frames.
//
// Frames are produced on the GPU: once per sample interval the main canvas
// texture (or only the selected region of it) is drawn into a small
// kAnalyzeWidth x kAnalyzeHeight render target and copied into a staging
// surface. The staging surface is mapped on the *next* sample (double
// buffered), so the CPU never waits for the GPU and only ~147 KB is read
// back per sample instead of a full-resolution frame every frame.
//
// Threading:
//  - Settings/lifecycle methods are called on the UI thread.
//  - Analysis runs on the OBS graphics thread (main rendered callback).
//  - Everything shared between the two is atomic; frame buffers are only
//    touched by the graphics thread while the callback is registered.
//
// Does not call OBS stop APIs; exposes shouldAutoStop() for the evaluator.
class MotionDetector {
public:
	using clock = std::chrono::steady_clock;
	using seconds = std::chrono::seconds;

	static constexpr uint32_t kAnalyzeWidth = 256;
	static constexpr uint32_t kAnalyzeHeight = 144;

	MotionDetector() = default;
	~MotionDetector();

	MotionDetector(const MotionDetector &) = delete;
	MotionDetector &operator=(const MotionDetector &) = delete;

	void setEnabled(bool enabled);
	bool isEnabled() const { return enabled_; }

	void setInactivityDuration(seconds duration);
	seconds inactivityDuration() const { return inactivity_duration_; }

	// Sensitivity threshold for motion score (changed-pixel %; e.g. 0.5 = 0.5%).
	void setSensitivityPercent(double percent);
	double sensitivityPercent() const { return sensitivity_percent_.load(); }

	// Ignore stillness until the recording has lasted at least this long.
	void setMinimumRecordingDuration(seconds duration);
	seconds minimumRecordingDuration() const { return min_recording_duration_; }

	// Monitor only a percentage region of the canvas.
	// When disabled, the full frame is used. Values are clamped to 0-100.
	void setRegionEnabled(bool enabled);
	bool isRegionEnabled() const { return region_enabled_; }
	void setRegionPercent(int x, int y, int w, int h);
	void regionPercent(int &x, int &y, int &w, int &h) const;

	bool isMonitoring() const { return monitoring_; }
	double lastMotionPercent() const { return last_motion_percent_.load(); }
	seconds stillnessDuration() const;

	bool shouldAutoStop(seconds recordingElapsed) const;

	void onRecordingStarted();
	void onRecordingStopped();
	// Pause stops frame analysis; resume restarts the stillness count.
	void onRecordingPaused();
	void onRecordingResumed();

	// Motion score = percent of pixels whose luma changed by >= kPixelDelta.
	static double changedPixelPercent(const uint8_t *a, const uint8_t *b,
					  size_t count);

private:
	void startMonitoring();
	void ensureCallbackRegistered();
	void ensureCallbackRemoved();
	void requestReset();
	void publishRegion();
	void clampRegion();

	static void renderedCallback(void *param);
	void onRendered();                                // graphics thread
	void analyzeMapped(const uint8_t *rgba, uint32_t linesize); // graphics thread
	void destroyGraphics();                           // needs graphics context

	// --- UI-thread state ---
	bool enabled_ = false;
	seconds inactivity_duration_{15};
	seconds min_recording_duration_{5 * 60};
	bool region_enabled_ = false;
	int region_x_percent_ = 0;
	int region_y_percent_ = 0;
	int region_w_percent_ = 100;
	int region_h_percent_ = 100;
	bool monitoring_ = false;
	bool recording_active_ = false;
	bool paused_ = false;
	bool callback_registered_ = false;

	// --- Shared (UI <-> graphics) ---
	std::atomic<double> sensitivity_percent_{0.5};
	// enabled(1) | x(7) | y(7) | w(7) | h(7), published as one word so the
	// graphics thread never sees a half-updated region.
	std::atomic<uint32_t> region_packed_{0};
	std::atomic<uint32_t> reset_generation_{0};
	std::atomic<int64_t> still_since_ns_{0}; // steady_clock ns; 0 = not still
	std::atomic<double> last_motion_percent_{0.0};

	// --- Graphics-thread state ---
	gs_texture_render *texrender_ = nullptr;
	gs_stage_surface *stage_[2] = {nullptr, nullptr};
	bool staged_[2] = {false, false};
	int next_stage_ = 0;
	uint64_t last_frame_ts_ = 0;
	uint64_t last_sample_ts_ = 0;
	uint32_t seen_generation_ = 0;
	std::vector<uint8_t> prev_luma_;
	std::vector<uint8_t> cur_luma_;
	bool have_prev_ = false;
	uint64_t last_debug_log_ts_ = 0;
};
