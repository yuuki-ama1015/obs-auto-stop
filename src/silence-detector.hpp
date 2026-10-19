#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

struct audio_data;

// Detects sustained silence on the main program audio mix (track 1).
//
// Lifecycle/settings run on the UI thread; the audio callback is only
// registered while monitoring and shares state through atomics only, so the
// audio thread never takes a lock.
class SilenceDetector {
public:
	using clock = std::chrono::steady_clock;
	using seconds = std::chrono::seconds;

	SilenceDetector() = default;
	~SilenceDetector();

	SilenceDetector(const SilenceDetector &) = delete;
	SilenceDetector &operator=(const SilenceDetector &) = delete;

	void setEnabled(bool enabled);
	bool isEnabled() const { return enabled_; }

	void setSilenceDuration(seconds duration);
	seconds silenceDuration() const { return silence_duration_; }

	// Threshold in dBFS (e.g. -50). Levels at or below count as silence.
	void setThresholdDb(double db);
	double thresholdDb() const { return threshold_db_; }

	void setMinimumRecordingDuration(seconds duration);
	seconds minimumRecordingDuration() const { return min_recording_duration_; }

	double lastLevelDb() const;
	seconds silenceActiveDuration() const;
	bool shouldAutoStop(seconds recordingElapsed) const;

	void onRecordingStarted();
	void onRecordingStopped();
	void onRecordingPaused();
	void onRecordingResumed();

private:
	void startMonitoring();
	void ensureCallbackRegistered();
	void ensureCallbackRemoved();
	void resetSilence();
	void onAudio(const struct audio_data *data);
	static void rawAudioCallback(void *param, size_t mix_idx,
				     struct audio_data *data);
	static float peakFromAudio(const struct audio_data *data, size_t channels);

	// --- UI thread ---
	bool enabled_ = false;
	seconds silence_duration_{15};
	double threshold_db_{-50.0};
	seconds min_recording_duration_{5 * 60};
	bool monitoring_ = false;
	bool recording_active_ = false;
	bool paused_ = false;
	bool callback_registered_ = false;
	size_t channels_ = 2;

	// --- Shared with the audio thread ---
	std::atomic<float> threshold_linear_{0.00316227766f}; // -50 dBFS
	std::atomic<float> last_peak_{0.0f};
	std::atomic<int64_t> silence_since_ns_{0}; // steady ns; 0 = not silent
};
