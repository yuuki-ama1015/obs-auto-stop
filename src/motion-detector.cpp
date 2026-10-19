#include "motion-detector.hpp"

#include <obs-module.h>
#include <obs.h>
#include <graphics/graphics.h>

#include <algorithm>

namespace {
// Sample interval (~8 per second), independent of the canvas frame rate.
constexpr uint64_t kSampleIntervalNs = 125'000'000ull;
constexpr int kPixelDelta = 12;

int clampPercent(int value)
{
	return std::max(0, std::min(100, value));
}

int64_t steadyNowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		       std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

uint32_t packRegion(bool enabled, int x, int y, int w, int h)
{
	return (enabled ? 1u : 0u) | (static_cast<uint32_t>(x) << 1) |
	       (static_cast<uint32_t>(y) << 8) |
	       (static_cast<uint32_t>(w) << 15) |
	       (static_cast<uint32_t>(h) << 22);
}

struct RegionPx {
	uint32_t x, y, w, h;
};

RegionPx resolveRegion(uint32_t packed, uint32_t texW, uint32_t texH)
{
	if ((packed & 1u) == 0) {
		return {0, 0, texW, texH};
	}
	const uint64_t px = (packed >> 1) & 0x7f;
	const uint64_t py = (packed >> 8) & 0x7f;
	const uint64_t pw = (packed >> 15) & 0x7f;
	const uint64_t ph = (packed >> 22) & 0x7f;
	RegionPx r{};
	r.x = static_cast<uint32_t>(texW * px / 100u);
	r.y = static_cast<uint32_t>(texH * py / 100u);
	const uint32_t x2 = static_cast<uint32_t>(texW * std::min<uint64_t>(px + pw, 100u) / 100u);
	const uint32_t y2 = static_cast<uint32_t>(texH * std::min<uint64_t>(py + ph, 100u) / 100u);
	r.x = std::min(r.x, texW - 1);
	r.y = std::min(r.y, texH - 1);
	r.w = std::max(1u, std::min(x2, texW) > r.x ? std::min(x2, texW) - r.x : 1u);
	r.h = std::max(1u, std::min(y2, texH) > r.y ? std::min(y2, texH) - r.y : 1u);
	return r;
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
		monitoring_ = false;
		requestReset();
	} else if (recording_active_ && !monitoring_) {
		// Enabled mid-recording: start watching right away.
		startMonitoring();
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
	sensitivity_percent_.store(std::max(0.1, std::min(50.0, percent)));
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
	publishRegion();
}

void MotionDetector::setRegionPercent(int x, int y, int w, int h)
{
	region_x_percent_ = clampPercent(x);
	region_y_percent_ = clampPercent(y);
	region_w_percent_ = clampPercent(w);
	region_h_percent_ = clampPercent(h);
	clampRegion();
	publishRegion();
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
	region_w_percent_ = std::max(1, region_w_percent_);
	region_h_percent_ = std::max(1, region_h_percent_);
	if (region_x_percent_ + region_w_percent_ > 100) {
		region_x_percent_ = 100 - region_w_percent_;
	}
	if (region_y_percent_ + region_h_percent_ > 100) {
		region_y_percent_ = 100 - region_h_percent_;
	}
}

void MotionDetector::publishRegion()
{
	region_packed_.store(packRegion(region_enabled_, region_x_percent_,
					region_y_percent_, region_w_percent_,
					region_h_percent_));
	// Region changed: previous frame is no longer comparable.
	requestReset();
}

void MotionDetector::requestReset()
{
	still_since_ns_.store(0);
	last_motion_percent_.store(0.0);
	sample_count_.store(0);
	reset_generation_.fetch_add(1);
}

MotionDetector::seconds MotionDetector::stillnessDuration() const
{
	const int64_t since = still_since_ns_.load();
	if (since == 0) {
		return seconds{0};
	}
	return std::chrono::duration_cast<seconds>(
		std::chrono::nanoseconds{std::max<int64_t>(0, steadyNowNs() - since)});
}

bool MotionDetector::shouldAutoStop(seconds recordingElapsed) const
{
	if (!enabled_ || !monitoring_ || paused_) {
		return false;
	}
	if (recordingElapsed < min_recording_duration_) {
		return false;
	}
	if (still_since_ns_.load() == 0) {
		return false;
	}
	return stillnessDuration() >= inactivity_duration_;
}

void MotionDetector::onRecordingStarted()
{
	recording_active_ = true;
	paused_ = false;
	monitoring_ = false;
	if (enabled_) {
		startMonitoring();
	}
}

void MotionDetector::startMonitoring()
{
	monitoring_ = true;
	requestReset();
	if (!paused_) {
		ensureCallbackRegistered();
	}
	blog(LOG_INFO,
	     "OBS Auto Stop: motion detection started (inactivity %lld s, sensitivity %.2f%%, min rec %lld s, region %s, gpu %ux%u)",
	     static_cast<long long>(inactivity_duration_.count()),
	     sensitivity_percent_.load(),
	     static_cast<long long>(min_recording_duration_.count()),
	     region_enabled_ ? "on" : "full-frame", kAnalyzeWidth,
	     kAnalyzeHeight);
}

void MotionDetector::onRecordingPaused()
{
	paused_ = true;
	ensureCallbackRemoved();
	requestReset();
}

void MotionDetector::onRecordingResumed()
{
	paused_ = false;
	requestReset();
	if (monitoring_) {
		ensureCallbackRegistered();
	}
}

void MotionDetector::onRecordingStopped()
{
	recording_active_ = false;
	paused_ = false;
	monitoring_ = false;
	ensureCallbackRemoved();
	requestReset();
}

void MotionDetector::ensureCallbackRegistered()
{
	if (callback_registered_) {
		return;
	}
	// Graphics-thread state is reset here while no callback can run.
	staged_[0] = staged_[1] = false;
	next_stage_ = 0;
	last_frame_ts_ = 0;
	last_sample_ts_ = 0;
	have_prev_ = false;
	have_ref_ = false;
	seen_generation_ = reset_generation_.load();
	obs_add_main_rendered_callback(renderedCallback, this);
	callback_registered_ = true;
}

void MotionDetector::ensureCallbackRemoved()
{
	if (!callback_registered_) {
		return;
	}
	// Rendered callbacks run under libobs' draw-callback mutex, so once this
	// returns the callback is not executing and will not run again.
	obs_remove_main_rendered_callback(renderedCallback, this);
	callback_registered_ = false;
	destroyGraphics();
	prev_luma_.clear();
	prev_luma_.shrink_to_fit();
	cur_luma_.clear();
	cur_luma_.shrink_to_fit();
	ref_luma_.clear();
	ref_luma_.shrink_to_fit();
	have_prev_ = false;
	have_ref_ = false;
}

void MotionDetector::destroyGraphics()
{
	if (!texrender_ && !stage_[0] && !stage_[1]) {
		return;
	}
	if (!obs_initialized()) {
		// libobs already shut down; its graphics objects are gone.
		texrender_ = nullptr;
		stage_[0] = stage_[1] = nullptr;
		return;
	}
	obs_enter_graphics();
	gs_texrender_destroy(texrender_);
	gs_stagesurface_destroy(stage_[0]);
	gs_stagesurface_destroy(stage_[1]);
	obs_leave_graphics();
	texrender_ = nullptr;
	stage_[0] = stage_[1] = nullptr;
	staged_[0] = staged_[1] = false;
}

void MotionDetector::renderedCallback(void *param)
{
	static_cast<MotionDetector *>(param)->onRendered();
}

void MotionDetector::onRendered()
{
	// Called once per video mix per frame; only act once per frame.
	const uint64_t frame_ts = obs_get_video_frame_time();
	if (frame_ts == last_frame_ts_) {
		return;
	}
	last_frame_ts_ = frame_ts;
	if (last_sample_ts_ != 0 && frame_ts - last_sample_ts_ < kSampleIntervalNs) {
		return;
	}
	last_sample_ts_ = frame_ts;

	const uint32_t gen = reset_generation_.load();
	if (gen != seen_generation_) {
		seen_generation_ = gen;
		have_prev_ = false;
		have_ref_ = false;
		staged_[0] = staged_[1] = false; // staged before the reset
		still_since_ns_.store(0);
	}

	gs_texture_t *tex = obs_get_main_texture();
	if (!tex) {
		return;
	}
	const uint32_t tex_w = gs_texture_get_width(tex);
	const uint32_t tex_h = gs_texture_get_height(tex);
	if (tex_w == 0 || tex_h == 0) {
		return;
	}

	// 1) Read back the sample staged last time (GPU finished long ago).
	const int read_idx = next_stage_ ^ 1;
	if (staged_[read_idx] && stage_[read_idx]) {
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(stage_[read_idx], &data, &linesize)) {
			analyzeMapped(data, linesize);
			gs_stagesurface_unmap(stage_[read_idx]);
		}
		staged_[read_idx] = false;
	}

	// 2) Draw the region into the small target and stage it.
	if (!texrender_) {
		texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	}
	gs_stage_surface *&stage = stage_[next_stage_];
	if (!stage) {
		stage = gs_stagesurface_create(kAnalyzeWidth, kAnalyzeHeight, GS_RGBA);
	}
	if (!texrender_ || !stage) {
		return;
	}

	const RegionPx r = resolveRegion(region_packed_.load(), tex_w, tex_h);

	gs_texrender_reset(texrender_);
	if (gs_texrender_begin(texrender_, kAnalyzeWidth, kAnalyzeHeight)) {
		gs_ortho(0.0f, static_cast<float>(r.w), 0.0f,
			 static_cast<float>(r.h), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_enable_blending(false);
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, tex);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite_subregion(tex, 0, r.x, r.y, r.w, r.h);
		}
		// The main texture may become the render target again right after
		// gs_texrender_end(); D3D11 would then silently drop the SRV while
		// libobs' binding cache still thinks it is bound. Unbind explicitly.
		gs_load_texture(nullptr, 0);
		gs_blend_state_pop();
		gs_texrender_end(texrender_);

		gs_stage_texture(stage, gs_texrender_get_texture(texrender_));
		staged_[next_stage_] = true;
		next_stage_ ^= 1;
	}
}

void MotionDetector::analyzeMapped(const uint8_t *rgba, uint32_t linesize)
{
	constexpr size_t count =
		static_cast<size_t>(kAnalyzeWidth) * kAnalyzeHeight;
	cur_luma_.resize(count);
	for (uint32_t y = 0; y < kAnalyzeHeight; ++y) {
		const uint8_t *src = rgba + static_cast<size_t>(y) * linesize;
		uint8_t *dst = cur_luma_.data() + static_cast<size_t>(y) * kAnalyzeWidth;
		for (uint32_t x = 0; x < kAnalyzeWidth; ++x) {
			const uint32_t r = src[x * 4 + 0];
			const uint32_t g = src[x * 4 + 1];
			const uint32_t b = src[x * 4 + 2];
			// BT.709 luma, integer weights summing to 256.
			dst[x] = static_cast<uint8_t>((54u * r + 183u * g + 19u * b) >> 8);
		}
	}

	if (have_prev_ && prev_luma_.size() == count) {
		const double sens = sensitivity_percent_.load();
		double motion =
			changedPixelPercent(prev_luma_.data(), cur_luma_.data(), count);
		if (motion < sens && have_ref_ && ref_luma_.size() == count) {
			// Catch slow drifts (fades, slow scrolls) against the frame
			// where stillness began.
			motion = std::max(motion, changedPixelPercent(ref_luma_.data(),
								     cur_luma_.data(), count));
		}
		last_motion_percent_.store(motion);
		if (motion >= sens) {
			still_since_ns_.store(0);
			have_ref_ = false;
		} else if (still_since_ns_.load() == 0 || !have_ref_) {
			if (still_since_ns_.load() == 0) {
				still_since_ns_.store(steadyNowNs());
			}
			ref_luma_ = cur_luma_;
			have_ref_ = true;
		}

		const uint64_t now = obs_get_video_frame_time();
		if (now - last_debug_log_ts_ >= 5'000'000'000ull) {
			last_debug_log_ts_ = now;
			blog(LOG_DEBUG,
			     "OBS Auto Stop: motion=%.2f%% sens=%.2f%% still=%lld s",
			     motion, sens,
			     static_cast<long long>(stillnessDuration().count()));
		}
	}
	sample_count_.fetch_add(1, std::memory_order_relaxed);

	prev_luma_.swap(cur_luma_);
	have_prev_ = true;
}

double MotionDetector::changedPixelPercent(const uint8_t *a, const uint8_t *b,
					   size_t count)
{
	// Mean-abs-diff diluted typical video to ~1% on a small downsample, so
	// the score is the share of pixels whose luma moved by >= kPixelDelta.
	if (count == 0) {
		return 0.0;
	}
	size_t changed = 0;
	for (size_t i = 0; i < count; ++i) {
		const uint8_t x = a[i];
		const uint8_t y = b[i];
		const uint8_t d = x > y ? static_cast<uint8_t>(x - y)
					: static_cast<uint8_t>(y - x);
		changed += d >= kPixelDelta ? 1u : 0u;
	}
	return static_cast<double>(changed) * 100.0 / static_cast<double>(count);
}
