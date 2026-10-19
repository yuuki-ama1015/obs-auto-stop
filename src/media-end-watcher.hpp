#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

struct obs_source;
struct obs_scene;
struct obs_scene_item;
struct calldata;
typedef struct obs_scene obs_scene_t;
typedef struct obs_scene_item obs_sceneitem_t;

// Stops recording when a non-looping media source in the program scene ends.
//
// Lifecycle methods run on the UI thread; media signals arrive on media
// threads and only touch the mutex-protected ended_/ended_name_ state.
class MediaEndWatcher {
public:
	using seconds = std::chrono::seconds;

	MediaEndWatcher() = default;
	~MediaEndWatcher();

	MediaEndWatcher(const MediaEndWatcher &) = delete;
	MediaEndWatcher &operator=(const MediaEndWatcher &) = delete;

	void setEnabled(bool enabled);
	bool isEnabled() const { return enabled_; }

	void setMinimumRecordingDuration(seconds duration);
	seconds minimumRecordingDuration() const { return min_recording_duration_; }

	bool shouldAutoStop(seconds recordingElapsed) const;
	std::string endedSourceName() const;
	// Number of media sources currently watched (UI thread).
	size_t watchedCount() const { return hooked_.size(); }
	bool isMonitoring() const { return monitoring_; }

	void onRecordingStarted();
	void onRecordingStopped();
	// Program scene changed: watch the new scene's media (ended latch kept).
	void onSceneChanged();
	// Scene collection is being torn down: drop every source reference now.
	void releaseAll();

private:
	void startMonitoring();
	void clearHooks();
	void hookCurrentScene();
	void hookSource(obs_source *source);
	static bool enumSceneItem(obs_scene_t *scene, obs_sceneitem_t *item,
				  void *param);
	static void onMediaEnded(void *param, struct calldata *data);
	static void onMediaRestarted(void *param, struct calldata *data);
	static bool sourceIsLooping(obs_source *source);
	static bool sourceIsMedia(obs_source *source);

	bool enabled_ = false;
	seconds min_recording_duration_{5 * 60};

	bool monitoring_ = false;
	bool recording_active_ = false;

	mutable std::mutex mutex_;
	bool watching_ = false; // guarded by mutex_ (read by media threads)
	bool ended_ = false;
	std::string ended_name_;

	std::vector<obs_source *> hooked_;
	std::unordered_set<obs_source *> hooked_set_;
};
