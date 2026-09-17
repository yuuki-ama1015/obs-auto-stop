#include "motion-detector.hpp"

#include <obs-module.h>
#include <media-io/video-io.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr uint32_t kAnalyzeWidth = 160;
constexpr uint32_t kAnalyzeHeight = 90;
// Sample ~2 fps to keep CPU cost low.
constexpr uint32_t kFrameRateDivisor = 15;
} // namespace

MotionDetector::~MotionDetector()
{
	ensureCallbackRemoved();
}

void MotionDetector::setEnabled(bool enabled)
{
	enabled_ = enabled;
	if (!enabled_) {
		ensureCallbackRemoved();
		resetStillness();
		monitoring_ = false;
		prev_luma_.clear();
	}
}

void MotionDetector::setInactivityDuration(seconds duration)
{
	if (duration.count() < 1) {
		duration = seconds{1};
	}
	inactivity_duration_ = duration;
}

void MotionDetector::setSensitivityPercent(double percent)
{
	if (percent < 0.1) {
		percent = 0.1;
	}
	if (percent > 50.0) {
		percent = 50.0;
	}
	sensitivity_percent_ = percent;
}

void MotionDetector::setMinimumRecordingDuration(seconds duration)
{
	if (duration.count() < 0) {
		duration = seconds{0};
	}
	min_recording_duration_ = duration;
}

MotionDetector::seconds MotionDetector::stillnessDuration() const
{
	if (!stillness_active_) {
		return seconds{0};
	}
	return std::chrono::duration_cast<seconds>(clock::now() - stillness_started_);
}

bool MotionDetector::shouldAutoStop(seconds recordingElapsed) const
{
	if (!enabled_ || !monitoring_) {
		return false;
	}
	if (recordingElapsed < min_recording_duration_) {
		return false;
	}
	if (!stillness_active_) {
		return false;
	}
	return stillnessDuration() >= inactivity_duration_;
}

void MotionDetector::onRecordingStarted()
{
	monitoring_ = enabled_;
	resetStillness();
	prev_luma_.clear();
	last_motion_percent_ = 0.0;
	if (monitoring_) {
		ensureCallbackRegistered();
		blog(LOG_INFO,
		     "OBS Auto Stop: motion detection started (inactivity %lld s, sensitivity %.2f%%, min rec %lld s)",
		     static_cast<long long>(inactivity_duration_.count()),
		     sensitivity_percent_,
		     static_cast<long long>(min_recording_duration_.count()));
	}
}

void MotionDetector::onRecordingStopped()
{
	monitoring_ = false;
	ensureCallbackRemoved();
	resetStillness();
	prev_luma_.clear();
	last_motion_percent_ = 0.0;
}

void MotionDetector::onVideoFrame(const struct video_data *frame)
{
	if (!enabled_ || !monitoring_ || !frame || !frame->data[0]) {
		return;
	}

	const uint32_t w = frame_width_ ? frame_width_ : kAnalyzeWidth;
	const uint32_t h = frame_height_ ? frame_height_ : kAnalyzeHeight;
	const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
	const uint8_t *cur = frame->data[0];
	const uint32_t stride = frame->linesize[0];

	// If the converter gave a tightly packed plane, linesize ~= width.
	// Otherwise copy row-by-row into a packed buffer for comparison.
	std::vector<uint8_t> packed;
	const uint8_t *a = cur;
	if (stride != w) {
		packed.resize(count);
		for (uint32_t y = 0; y < h; ++y) {
			const uint8_t *src = cur + static_cast<size_t>(y) * stride;
			uint8_t *dst = packed.data() + static_cast<size_t>(y) * w;
			std::copy(src, src + w, dst);
		}
		a = packed.data();
	}

	if (prev_luma_.size() == count) {
		last_motion_percent_ = meanAbsDiffPercent(prev_luma_.data(), a, count);
		if (last_motion_percent_ >= sensitivity_percent_) {
			resetStillness();
		} else if (!stillness_active_) {
			stillness_started_ = clock::now();
			stillness_active_ = true;
		}
	}

	prev_luma_.assign(a, a + count);
}

struct video_scale_info MotionDetector::makeConversion()
{
	struct video_scale_info info {};
	info.format = VIDEO_FORMAT_Y800;
	info.width = kAnalyzeWidth;
	info.height = kAnalyzeHeight;
	info.range = VIDEO_RANGE_FULL;
	info.colorspace = VIDEO_CS_DEFAULT;
	return info;
}

void MotionDetector::ensureCallbackRegistered()
{
	if (callback_registered_) {
		return;
	}
	struct video_scale_info conversion = makeConversion();
	frame_width_ = conversion.width;
	frame_height_ = conversion.height;
	obs_add_raw_video_callback2(&conversion, kFrameRateDivisor,
				    [](void *param, struct video_data *frame) {
					    static_cast<MotionDetector *>(param)
						    ->onVideoFrame(frame);
				    },
				    this);
	callback_registered_ = true;
}

void MotionDetector::ensureCallbackRemoved()
{
	if (!callback_registered_) {
		return;
	}
	obs_remove_raw_video_callback(
		[](void *param, struct video_data *frame) {
			static_cast<MotionDetector *>(param)->onVideoFrame(frame);
		},
		this);
	callback_registered_ = false;
}

void MotionDetector::resetStillness()
{
	stillness_active_ = false;
	stillness_started_ = clock::time_point{};
}

double MotionDetector::meanAbsDiffPercent(const uint8_t *a, const uint8_t *b,
					  size_t count)
{
	if (count == 0) {
		return 0.0;
	}
	uint64_t acc = 0;
	// Subsample for speed on larger buffers.
	const size_t step = count > 4096 ? 4 : 1;
	size_t samples = 0;
	for (size_t i = 0; i < count; i += step) {
		acc += static_cast<uint64_t>(
			std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
		++samples;
	}
	if (samples == 0) {
		return 0.0;
	}
	const double mean = static_cast<double>(acc) / static_cast<double>(samples);
	return (mean / 255.0) * 100.0;
}
