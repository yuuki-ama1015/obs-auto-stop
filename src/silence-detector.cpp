#include "silence-detector.hpp"

#include <obs-module.h>
#include <media-io/audio-io.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
	if (db > 0.0) {
		db = 0.0;
	}
	if (db < -100.0) {
		db = -100.0;
	}
	threshold_db_ = db;
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
	std::lock_guard<std::mutex> lock(mutex_);
	return last_level_db_;
}

SilenceDetector::seconds SilenceDetector::silenceActiveDuration() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!silence_active_) {
		return seconds{0};
	}
	return std::chrono::duration_cast<seconds>(clock::now() -
						   silence_started_);
}

bool SilenceDetector::shouldAutoStop(seconds recordingElapsed) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!enabled_ || !monitoring_ || !silence_active_) {
		return false;
	}
	if (recordingElapsed < min_recording_duration_) {
		return false;
	}
	const auto elapsed = std::chrono::duration_cast<seconds>(
		clock::now() - silence_started_);
	return elapsed >= silence_duration_;
}

void SilenceDetector::onRecordingStarted()
{
	resetSilence();
	monitoring_ = enabled_;
	if (monitoring_) {
		ensureCallbackRegistered();
		blog(LOG_INFO,
		     "OBS Auto Stop: silence detection started (duration %lld s, threshold %.1f dB, min rec %lld s)",
		     static_cast<long long>(silence_duration_.count()),
		     threshold_db_,
		     static_cast<long long>(min_recording_duration_.count()));
	}
}

void SilenceDetector::onRecordingStopped()
{
	monitoring_ = false;
	ensureCallbackRemoved();
	resetSilence();
}

void SilenceDetector::resetSilence()
{
	std::lock_guard<std::mutex> lock(mutex_);
	silence_active_ = false;
	silence_started_ = clock::time_point{};
	last_level_db_ = -120.0;
}

void SilenceDetector::ensureCallbackRegistered()
{
	if (callback_registered_) {
		return;
	}
	struct audio_convert_info info {};
	info.format = AUDIO_FORMAT_FLOAT_PLANAR;
	info.speakers = SPEAKERS_STEREO;
	info.samples_per_sec = 48000;
	audio_t *audio = obs_get_audio();
	if (audio) {
		const struct audio_output_info *aoi = audio_output_get_info(audio);
		if (aoi) {
			info.speakers = aoi->speakers;
			info.samples_per_sec = aoi->samples_per_sec;
		}
	}
	obs_add_raw_audio_callback(0, &info, rawAudioCallback, this);
	callback_registered_ = true;
}

void SilenceDetector::ensureCallbackRemoved()
{
	if (!callback_registered_) {
		return;
	}
	obs_remove_raw_audio_callback(0, rawAudioCallback, this);
	callback_registered_ = false;
}

double SilenceDetector::peakDbFromAudio(const struct audio_data *data)
{
	if (!data || data->frames == 0 || !data->data[0]) {
		return -120.0;
	}
	const float *samples = reinterpret_cast<const float *>(data->data[0]);
	float peak = 0.0f;
	for (uint32_t i = 0; i < data->frames; ++i) {
		peak = std::max(peak, std::fabs(samples[i]));
	}
	// Also peek other planes if present
	for (size_t ch = 1; ch < MAX_AV_PLANES; ++ch) {
		if (!data->data[ch]) {
			break;
		}
		const float *s = reinterpret_cast<const float *>(data->data[ch]);
		for (uint32_t i = 0; i < data->frames; ++i) {
			peak = std::max(peak, std::fabs(s[i]));
		}
	}
	if (peak < 1.0e-8f) {
		return -120.0;
	}
	return 20.0 * std::log10(static_cast<double>(peak));
}

void SilenceDetector::onAudio(struct audio_data *data)
{
	if (!enabled_ || !monitoring_ || !data) {
		return;
	}
	const double level = peakDbFromAudio(data);
	std::lock_guard<std::mutex> lock(mutex_);
	last_level_db_ = level;
	if (level <= threshold_db_) {
		if (!silence_active_) {
			silence_active_ = true;
			silence_started_ = clock::now();
		}
	} else {
		silence_active_ = false;
		silence_started_ = clock::time_point{};
	}
}

void SilenceDetector::rawAudioCallback(void *param, size_t /*mix_idx*/,
				       struct audio_data *data)
{
	static_cast<SilenceDetector *>(param)->onAudio(data);
}
