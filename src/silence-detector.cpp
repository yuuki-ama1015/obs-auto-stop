#include "silence-detector.hpp"

#include <obs-module.h>
#include <media-io/audio-io.h>

#include <algorithm>
#include <cmath>

namespace {
int64_t steadyNowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		       std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
} // namespace

SilenceDetector::~SilenceDetector()
{
	ensureCallbackRemoved();
}

void SilenceDetector::setEnabled(bool enabled)
{
	enabled_ = enabled;
	if (!enabled_) {
		ensureCallbackRemoved();
		resetSilence();
		monitoring_ = false;
	} else if (recording_active_ && !monitoring_) {
		// Enabled mid-recording: start watching right away.
		startMonitoring();
	}
}

void SilenceDetector::setSilenceDuration(seconds duration)
{
	if (duration.count() < 1) {
		duration = seconds{1};
	}
	silence_duration_ = duration;
}

void SilenceDetector::setThresholdDb(double db)
{
	threshold_db_ = std::max(-100.0, std::min(0.0, db));
	threshold_linear_.store(static_cast<float>(std::pow(10.0, threshold_db_ / 20.0)));
}

void SilenceDetector::setMinimumRecordingDuration(seconds duration)
{
	if (duration.count() < 0) {
		duration = seconds{0};
	}
	min_recording_duration_ = duration;
}

double SilenceDetector::lastLevelDb() const
{
	const float peak = last_peak_.load();
	if (peak < 1.0e-6f) {
		return -120.0;
	}
	return 20.0 * std::log10(static_cast<double>(peak));
}

SilenceDetector::seconds SilenceDetector::silenceActiveDuration() const
{
	const int64_t since = silence_since_ns_.load();
	if (since == 0) {
		return seconds{0};
	}
	return std::chrono::duration_cast<seconds>(
		std::chrono::nanoseconds{std::max<int64_t>(0, steadyNowNs() - since)});
}

bool SilenceDetector::shouldAutoStop(seconds recordingElapsed) const
{
	if (!enabled_ || !monitoring_ || paused_) {
		return false;
	}
	if (recordingElapsed < min_recording_duration_) {
		return false;
	}
	if (silence_since_ns_.load() == 0) {
		return false;
	}
	return silenceActiveDuration() >= silence_duration_;
}

void SilenceDetector::onRecordingStarted()
{
	recording_active_ = true;
	paused_ = false;
	monitoring_ = false;
	resetSilence();
	if (enabled_) {
		startMonitoring();
	}
}

void SilenceDetector::startMonitoring()
{
	resetSilence();
	monitoring_ = true;
	if (!paused_) {
		ensureCallbackRegistered();
	}
	blog(LOG_INFO,
	     "OBS Auto Stop: silence detection started (duration %lld s, threshold %.1f dB, min rec %lld s)",
	     static_cast<long long>(silence_duration_.count()), threshold_db_,
	     static_cast<long long>(min_recording_duration_.count()));
}

void SilenceDetector::onRecordingPaused()
{
	paused_ = true;
	ensureCallbackRemoved();
	resetSilence();
}

void SilenceDetector::onRecordingResumed()
{
	paused_ = false;
	resetSilence();
	if (monitoring_) {
		ensureCallbackRegistered();
	}
}

void SilenceDetector::onRecordingStopped()
{
	recording_active_ = false;
	paused_ = false;
	monitoring_ = false;
	ensureCallbackRemoved();
	resetSilence();
}

void SilenceDetector::resetSilence()
{
	silence_since_ns_.store(0);
	last_peak_.store(0.0f);
}

void SilenceDetector::ensureCallbackRegistered()
{
	if (callback_registered_) {
		return;
	}
	audio_t *audio = obs_get_audio();
	if (!audio) {
		return;
	}
	channels_ = audio_output_get_channels(audio);
	if (channels_ == 0 || channels_ > MAX_AV_PLANES) {
		channels_ = std::min<size_t>(std::max<size_t>(channels_, 1), MAX_AV_PLANES);
	}
	// NULL conversion: OBS mixes are already planar float at the output
	// rate, so no resampler is created.
	obs_add_raw_audio_callback(0, nullptr, rawAudioCallback, this);
	callback_registered_ = true;
}

void SilenceDetector::ensureCallbackRemoved()
{
	if (!callback_registered_) {
		return;
	}
	// audio_output_disconnect() synchronizes with the audio thread, so the
	// callback is not running once this returns.
	obs_remove_raw_audio_callback(0, rawAudioCallback, this);
	callback_registered_ = false;
}

float SilenceDetector::peakFromAudio(const struct audio_data *data,
				     size_t channels)
{
	float peak = 0.0f;
	for (size_t ch = 0; ch < channels; ++ch) {
		const auto *s = reinterpret_cast<const float *>(data->data[ch]);
		if (!s) {
			break;
		}
		for (uint32_t i = 0; i < data->frames; ++i) {
			peak = std::max(peak, std::fabs(s[i]));
		}
	}
	return peak;
}

void SilenceDetector::onAudio(const struct audio_data *data)
{
	if (!data || data->frames == 0) {
		return;
	}
	const float peak = peakFromAudio(data, channels_);
	last_peak_.store(peak, std::memory_order_relaxed);
	if (peak <= threshold_linear_.load(std::memory_order_relaxed)) {
		if (silence_since_ns_.load(std::memory_order_relaxed) == 0) {
			silence_since_ns_.store(steadyNowNs());
		}
	} else {
		silence_since_ns_.store(0);
	}
}

void SilenceDetector::rawAudioCallback(void *param, size_t /*mix_idx*/,
				       struct audio_data *data)
{
	static_cast<SilenceDetector *>(param)->onAudio(data);
}
