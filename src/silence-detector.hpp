#pragma once

#include <chrono>
#include <mutex>

struct audio_data;

// Detects sustained silence on the main program audio mix.
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

private:
	void ensureCallbackRegistered();
	void ensureCallbackRemoved();
	void resetSilence();
	void onAudio(struct audio_data *data);
	static void rawAudioCallback(void *param, size_t mix_idx,
				     struct audio_data *data);
	static double peakDbFromAudio(const struct audio_data *data);

	bool enabled_ = false;
	seconds silence_duration_{15};
	double threshold_db_{-50.0};
	seconds min_recording_duration_{5 * 60};

	bool monitoring_ = false;
	bool callback_registered_ = false;

	mutable std::mutex mutex_;
	double last_level_db_{-120.0};
	bool silence_active_ = false;
	clock::time_point silence_started_{};
};
