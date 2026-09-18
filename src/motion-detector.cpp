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

int clampPercent(int value)
{
	if (value < 0) {
		return 0;
	}
	if (value > 100) {
		return 100;
	}
	return value;
}
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
		clearPreviousFrame();
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

void MotionDetector::setRegionEnabled(bool enabled)
{
	if (region_enabled_ == enabled) {
		return;
	}
	region_enabled_ = enabled;
	clearPreviousFrame();
	resetStillness();
}

void MotionDetector::setRegionPercent(int x, int y, int w, int h)
{
	region_x_percent_ = clampPercent(x);
	region_y_percent_ = clampPercent(y);
	region_w_percent_ = clampPercent(w);
	region_h_percent_ = clampPercent(h);
	clampRegion();
	clearPreviousFrame();
	resetStillness();
}

void MotionDetector::regionPercent(int &x, int &y, int &w, int &h) const
{
	x = region_x_percent_;
	y = region_y_percent_;
	w = region_w_percent_;
	h = region_h_percent_;
}

void MotionDetector::clampRegion()
{
	if (region_w_percent_ < 1) {
		region_w_percent_ = 1;
	}
	if (region_h_percent_ < 1) {
		region_h_percent_ = 1;
	}
	if (region_x_percent_ + region_w_percent_ > 100) {
		region_x_percent_ = 100 - region_w_percent_;
	}
	if (region_y_percent_ + region_h_percent_ > 100) {
		region_y_percent_ = 100 - region_h_percent_;
	}
}

void MotionDetector::resolveRegionPixels(uint32_t frameW, uint32_t frameH,
					 uint32_t &rx, uint32_t &ry,
					 uint32_t &rw, uint32_t &rh) const
{
	if (!region_enabled_) {
		rx = 0;
		ry = 0;
		rw = frameW;
		rh = frameH;
		return;
	}

	rx = static_cast<uint32_t>((static_cast<uint64_t>(frameW) *
				    static_cast<uint64_t>(region_x_percent_)) /
				   100ull);
	ry = static_cast<uint32_t>((static_cast<uint64_t>(frameH) *
				    static_cast<uint64_t>(region_y_percent_)) /
				   100ull);
	const uint32_t x2 =
		static_cast<uint32_t>((static_cast<uint64_t>(frameW) *
				       static_cast<uint64_t>(region_x_percent_ +
							     region_w_percent_)) /
				      100ull);
	const uint32_t y2 =
		static_cast<uint32_t>((static_cast<uint64_t>(frameH) *
				       static_cast<uint64_t>(region_y_percent_ +
							     region_h_percent_)) /
				      100ull);

	rw = x2 > rx ? x2 - rx : 1;
	rh = y2 > ry ? y2 - ry : 1;
	if (rx + rw > frameW) {
		rw = frameW - rx;
	}
	if (ry + rh > frameH) {
		rh = frameH - ry;
	}
	if (rw < 1) {
		rw = 1;
	}
	if (rh < 1) {
		rh = 1;
	}
}

MotionDetector::seconds MotionDetector::stillnessDuration() const
{
	if (!stillness_active_) {
		return seconds{0};
	}
	return std::chrono::duration_cast<seconds>(clock::now() -
						   stillness_started_);
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
	clearPreviousFrame();
	last_motion_percent_ = 0.0;
	if (monitoring_) {
		ensureCallbackRegistered();
		blog(LOG_INFO,
		     "OBS Auto Stop: motion detection started (inactivity %lld s, sensitivity %.2f%%, min rec %lld s, region %s)",
		     static_cast<long long>(inactivity_duration_.count()),
		     sensitivity_percent_,
		     static_cast<long long>(min_recording_duration_.count()),
		     region_enabled_ ? "on" : "full-frame");
	}
}

void MotionDetector::onRecordingStopped()
{
	monitoring_ = false;
	ensureCallbackRemoved();
	resetStillness();
	clearPreviousFrame();
	last_motion_percent_ = 0.0;
}

void MotionDetector::onVideoFrame(const struct video_data *frame)
{
	if (!enabled_ || !monitoring_ || !frame || !frame->data[0]) {
		return;
	}

	const uint32_t w = analyze_width_ ? analyze_width_ : kAnalyzeWidth;
	const uint32_t h = analyze_height_ ? analyze_height_ : kAnalyzeHeight;
	const uint8_t *cur = frame->data[0];
	const uint32_t stride = frame->linesize[0];

	uint32_t rx = 0, ry = 0, rw = w, rh = h;
	resolveRegionPixels(w, h, rx, ry, rw, rh);
	region_pixel_w_ = rw;
	region_pixel_h_ = rh;

	const size_t count = static_cast<size_t>(rw) * static_cast<size_t>(rh);
	std::vector<uint8_t> region;
	region.resize(count);

	for (uint32_t y = 0; y < rh; ++y) {
		const uint8_t *src =
			cur + static_cast<size_t>(ry + y) * stride + rx;
		uint8_t *dst = region.data() + static_cast<size_t>(y) * rw;
		std::copy(src, src + rw, dst);
	}

	if (prev_luma_.size() == count) {
		last_motion_percent_ =
			meanAbsDiffPercent(prev_luma_.data(), region.data(), count);
		if (last_motion_percent_ >= sensitivity_percent_) {
			resetStillness();
		} else if (!stillness_active_) {
			stillness_started_ = clock::now();
			stillness_active_ = true;
		}
		static auto last_log = clock::time_point{};
		const auto now = clock::now();
		if (last_log.time_since_epoch().count() == 0 ||
		    now - last_log >= std::chrono::seconds{1}) {
			last_log = now;
			blog(LOG_INFO,
			     "OBS Auto Stop: motion=%.2f%% sens=%.2f%% still=%lld/%lld region=%ux%u",
			     last_motion_percent_, sensitivity_percent_,
			     static_cast<long long>(stillnessDuration().count()),
			     static_cast<long long>(inactivity_duration_.count()),
			     region_pixel_w_, region_pixel_h_);
		}
	}

	prev_luma_ = std::move(region);
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

void MotionDetector::rawVideoCallback(void *param, struct video_data *frame)
{
	static_cast<MotionDetector *>(param)->onVideoFrame(frame);
}

void MotionDetector::ensureCallbackRegistered()
{
	if (callback_registered_) {
		return;
	}
	struct video_scale_info conversion = makeConversion();
	analyze_width_ = conversion.width;
	analyze_height_ = conversion.height;
	obs_add_raw_video_callback2(&conversion, kFrameRateDivisor,
				    rawVideoCallback, this);
	callback_registered_ = true;
}

void MotionDetector::ensureCallbackRemoved()
{
	if (!callback_registered_) {
		return;
	}
	obs_remove_raw_video_callback(rawVideoCallback, this);
	callback_registered_ = false;
}

void MotionDetector::resetStillness()
{
	stillness_active_ = false;
	stillness_started_ = clock::time_point{};
}

void MotionDetector::clearPreviousFrame()
{
	prev_luma_.clear();
	region_pixel_w_ = 0;
	region_pixel_h_ = 0;
}

double MotionDetector::meanAbsDiffPercent(const uint8_t *a, const uint8_t *b,
					  size_t count)
{
	if (count == 0) {
		return 0.0;
	}
	uint64_t acc = 0;
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
	const double mean =
		static_cast<double>(acc) / static_cast<double>(samples);
	return (mean / 255.0) * 100.0;
}
