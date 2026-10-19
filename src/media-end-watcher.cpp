#include "media-end-watcher.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.h>

#include <cstring>

namespace {

bool sourceSupportsMedia(obs_source_t *source)
{
	if (!source) {
		return false;
	}
	const uint32_t flags = obs_source_get_output_flags(source);
	if (flags & OBS_SOURCE_CONTROLLABLE_MEDIA) {
		return true;
	}
	const char *id = obs_source_get_id(source);
	if (!id) {
		return false;
	}
	return std::strcmp(id, "ffmpeg_source") == 0 ||
	       std::strcmp(id, "vlc_source") == 0;
}

// ffmpeg_source uses "looping"; vlc_source and the slideshow use "loop".
const char *loopSettingKey(obs_source_t *source)
{
	const char *id = obs_source_get_id(source);
	if (id && std::strcmp(id, "ffmpeg_source") == 0) {
		return "looping";
	}
	return "loop";
}

bool isLoopingMedia(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
	if (!settings) {
		return false;
	}
	const bool looping = obs_data_get_bool(settings, loopSettingKey(source));
	obs_data_release(settings);
	return looping;
}

constexpr const char *kRestartSignals[] = {"media_restart", "media_play",
					    "media_started"};

} // namespace

MediaEndWatcher::~MediaEndWatcher()
{
	clearHooks();
}

void MediaEndWatcher::setEnabled(bool enabled)
{
	enabled_ = enabled;
	if (!enabled_) {
		clearHooks();
		monitoring_ = false;
		std::lock_guard<std::mutex> lock(mutex_);
		watching_ = false;
		ended_ = false;
		ended_name_.clear();
	} else if (recording_active_ && !monitoring_) {
		// Enabled mid-recording: start watching right away.
		startMonitoring();
	}
}

void MediaEndWatcher::setMinimumRecordingDuration(seconds duration)
{
	if (duration.count() < 0) {
		duration = seconds{0};
	}
	min_recording_duration_ = duration;
}

bool MediaEndWatcher::shouldAutoStop(seconds recordingElapsed) const
{
	if (!enabled_ || !monitoring_) {
		return false;
	}
	if (recordingElapsed < min_recording_duration_) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	return ended_;
}

std::string MediaEndWatcher::endedSourceName() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return ended_name_;
}

void MediaEndWatcher::onRecordingStarted()
{
	recording_active_ = true;
	monitoring_ = false;
	clearHooks();
	{
		std::lock_guard<std::mutex> lock(mutex_);
		watching_ = false;
		ended_ = false;
		ended_name_.clear();
	}
	if (enabled_) {
		startMonitoring();
	}
}

void MediaEndWatcher::startMonitoring()
{
	monitoring_ = true;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		watching_ = true;
		ended_ = false;
		ended_name_.clear();
	}
	clearHooks();
	hookCurrentScene();
	blog(LOG_INFO,
	     "OBS Auto Stop: media-end watch started (min rec %lld s, sources %zu)",
	     static_cast<long long>(min_recording_duration_.count()),
	     hooked_.size());
}

void MediaEndWatcher::onRecordingStopped()
{
	recording_active_ = false;
	monitoring_ = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		watching_ = false;
		ended_ = false;
		ended_name_.clear();
	}
	clearHooks();
}

void MediaEndWatcher::onSceneChanged()
{
	if (!monitoring_) {
		return;
	}
	clearHooks();
	hookCurrentScene();
	blog(LOG_INFO, "OBS Auto Stop: program scene changed, watching %zu media source(s)",
	     hooked_.size());
}

void MediaEndWatcher::releaseAll()
{
	clearHooks();
}

void MediaEndWatcher::clearHooks()
{
	for (obs_source_t *source : hooked_) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_disconnect(sh, "media_ended", onMediaEnded, this);
			for (const char *sig : kRestartSignals) {
				signal_handler_disconnect(sh, sig, onMediaRestarted, this);
			}
		}
		obs_source_release(source);
	}
	hooked_.clear();
	hooked_set_.clear();
}

bool MediaEndWatcher::sourceIsMedia(obs_source *source)
{
	return sourceSupportsMedia(source);
}

bool MediaEndWatcher::sourceIsLooping(obs_source *source)
{
	return isLoopingMedia(source);
}

void MediaEndWatcher::hookSource(obs_source *source)
{
	if (!source || !sourceIsMedia(source)) {
		return;
	}
	if (hooked_set_.count(source)) {
		return;
	}
	if (sourceIsLooping(source)) {
		blog(LOG_INFO, "OBS Auto Stop: skip looping media source '%s'",
		     obs_source_get_name(source));
		return;
	}

	signal_handler_t *sh = obs_source_get_signal_handler(source);
	if (!sh) {
		return;
	}
	// get_ref returns NULL when the source is already being destroyed.
	if (!obs_source_get_ref(source)) {
		return;
	}
	signal_handler_connect(sh, "media_ended", onMediaEnded, this);
	for (const char *sig : kRestartSignals) {
		signal_handler_connect(sh, sig, onMediaRestarted, this);
	}
	hooked_.push_back(source);
	hooked_set_.insert(source);
	blog(LOG_INFO, "OBS Auto Stop: watching media end on '%s'",
	     obs_source_get_name(source));
}

bool MediaEndWatcher::enumSceneItem(obs_scene_t *, obs_sceneitem_t *item,
				    void *param)
{
	auto *self = static_cast<MediaEndWatcher *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source) {
		return true;
	}

	self->hookSource(source);

	// Nested scenes *and* groups (obs_scene_from_source() ignores groups).
	if (obs_scene_t *nested = obs_group_or_scene_from_source(source)) {
		obs_scene_enum_items(nested, enumSceneItem, self);
	}
	return true;
}

void MediaEndWatcher::hookCurrentScene()
{
	obs_source_t *prog = obs_frontend_get_current_scene();
	if (!prog) {
		return;
	}
	obs_scene_t *scene = obs_scene_from_source(prog);
	if (scene) {
		obs_scene_enum_items(scene, enumSceneItem, this);
	}
	obs_source_release(prog);
}

void MediaEndWatcher::onMediaEnded(void *param, struct calldata *data)
{
	auto *self = static_cast<MediaEndWatcher *>(param);
	obs_source_t *source =
		static_cast<obs_source_t *>(calldata_ptr(data, "source"));
	const char *name = source ? obs_source_get_name(source) : nullptr;

	std::lock_guard<std::mutex> lock(self->mutex_);
	if (!self->watching_) {
		return;
	}
	self->ended_ = true;
	self->ended_name_ = name ? name : "";
	blog(LOG_INFO, "OBS Auto Stop: media ended on '%s'", name ? name : "(unknown)");
}

void MediaEndWatcher::onMediaRestarted(void *param, struct calldata *data)
{
	// The media that ended was played again: it has not "finished" anymore.
	auto *self = static_cast<MediaEndWatcher *>(param);
	obs_source_t *source =
		static_cast<obs_source_t *>(calldata_ptr(data, "source"));
	const char *name = source ? obs_source_get_name(source) : nullptr;
	if (!name) {
		return;
	}
	std::lock_guard<std::mutex> lock(self->mutex_);
	if (self->ended_ && self->ended_name_ == name) {
		self->ended_ = false;
		self->ended_name_.clear();
		blog(LOG_INFO, "OBS Auto Stop: media '%s' restarted, end cleared", name);
	}
}
