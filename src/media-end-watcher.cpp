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

bool isLoopingMedia(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
	if (!settings) {
		return false;
	}
	const bool looping = obs_data_get_bool(settings, "looping");
	obs_data_release(settings);
	return looping;
}

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
		std::lock_guard<std::mutex> lock(mutex_);
		ended_ = false;
		ended_name_.clear();
		monitoring_ = false;
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
	std::lock_guard<std::mutex> lock(mutex_);
	if (!enabled_ || !monitoring_ || !ended_) {
		return false;
	}
	if (recordingElapsed < min_recording_duration_) {
		return false;
	}
	return true;
}

std::string MediaEndWatcher::endedSourceName() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return ended_name_;
}

void MediaEndWatcher::onRecordingStarted()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ended_ = false;
		ended_name_.clear();
		monitoring_ = enabled_;
	}
	clearHooks();
	if (enabled_) {
		hookCurrentScene();
		blog(LOG_INFO,
		     "OBS Auto Stop: media-end watch started (min rec %lld s, hooks %zu)",
		     static_cast<long long>(min_recording_duration_.count()),
		     hooked_.size());
	}
}

void MediaEndWatcher::onRecordingStopped()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		monitoring_ = false;
		ended_ = false;
		ended_name_.clear();
	}
	clearHooks();
}

void MediaEndWatcher::clearHooks()
{
	for (obs_source_t *source : hooked_) {
		if (!source) {
			continue;
		}
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_disconnect(sh, "media_ended", onMediaEnded,
						 this);
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
	if (sourceIsLooping(source)) {
		blog(LOG_INFO,
		     "OBS Auto Stop: skip looping media source '%s'",
		     obs_source_get_name(source));
		return;
	}
	if (hooked_set_.count(source)) {
		return;
	}

	signal_handler_t *sh = obs_source_get_signal_handler(source);
	if (!sh) {
		return;
	}
	obs_source_get_ref(source);
	signal_handler_connect(sh, "media_ended", onMediaEnded, this);
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

	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE) {
		obs_scene_t *nested = obs_scene_from_source(source);
		if (nested) {
			obs_scene_enum_items(nested, enumSceneItem, self);
		}
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
	const char *name = source ? obs_source_get_name(source) : "(unknown)";

	std::lock_guard<std::mutex> lock(self->mutex_);
	if (!self->monitoring_ || !self->enabled_) {
		return;
	}
	self->ended_ = true;
	self->ended_name_ = name ? name : "";
	blog(LOG_INFO, "OBS Auto Stop: media ended on '%s'", name);
}
