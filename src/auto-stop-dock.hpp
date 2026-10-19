#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QToolButton;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTimer;
class QShowEvent;
class QDockWidget;

class RecordingMonitor;
class MotionDetector;
class MediaEndWatcher;
class SilenceDetector;
class StopController;

class AutoStopDock : public QWidget {
	Q_OBJECT

public:
	AutoStopDock(RecordingMonitor *monitor, MotionDetector *motion,
		     MediaEndWatcher *media, SilenceDetector *silence,
		     StopController *stop, QWidget *parent = nullptr);
	~AutoStopDock() override;

	// Re-read settings from the (possibly new) profile config.
	void reloadSettings() { loadSettings(); }
	// Write a pending debounced save right now (profile switch, shutdown).
	void flushPendingSave();
	// Show that OBS_AUTOSTOP_* environment overrides are in effect.
	void setEnvOverrideActive(bool active);

private slots:
	void onAutoStopToggled(bool enabled);
	void onMaxMinutesChanged(int minutes);
	void onMotionToggled(bool enabled);
	void onInactivitySecondsChanged(int seconds);
	void onSensitivityChanged(double percent);
	void onMinRecordingMinutesChanged(int minutes);
	void onRegionToggled(bool enabled);
	void onSelectRegionClicked();
	void onMediaEndToggled(bool enabled);
	void onSilenceToggled(bool enabled);
	void onSilenceSecondsChanged(int seconds);
	void onSilenceThresholdChanged(double db);
	void refreshStatus();
	void onMinimizeToTaskbar();
	void onCombineModeChanged(int index);
	void onOpenRecordingFolder();

private:
	struct Settings {
		bool timerEnabled = true;
		int maxMinutes = 120;
		bool motionEnabled = false;
		int inactivitySec = 15;
		int minRecordingMin = 5;
		bool regionEnabled = false;
		int regionX = 0;
		int regionY = 0;
		int regionW = 100;
		int regionH = 100;
		bool mediaEndEnabled = false;
		int combineMode = 0;
		bool silenceEnabled = false;
		int silenceSec = 15;
		double silenceDb = -50.0;
	};

	void loadSettings();
	static Settings readSettings();
	void applySettingsToUi(const Settings &s);
	void applySettingsToModel(const Settings &s);
	void scheduleSave();
	void saveSettingsNow();
	void updateAndWarning();
	void updateRegionControlsEnabled();
	void updateRegionStatusLabel();
	void applyMinRecordingToAll(int minutes);
	void showEvent(QShowEvent *event) override;
	QDockWidget *hostDock() const;
	void ensureFloatingMinimizeButton();
	void updateCombineHint();

	RecordingMonitor *monitor_ = nullptr;
	MotionDetector *motion_ = nullptr;
	MediaEndWatcher *media_ = nullptr;
	SilenceDetector *silence_ = nullptr;
	StopController *stop_ = nullptr;

	QCheckBox *autoStopCheck_ = nullptr;
	QSpinBox *maxMinutesSpin_ = nullptr;
	QCheckBox *motionCheck_ = nullptr;
	QSpinBox *inactivitySpin_ = nullptr;
	QDoubleSpinBox *sensitivitySpin_ = nullptr;
	QSpinBox *minRecordingSpin_ = nullptr;
	QCheckBox *regionCheck_ = nullptr;
	QPushButton *selectRegionButton_ = nullptr;
	QLabel *regionStatusLabel_ = nullptr;
	QWidget *regionWidget_ = nullptr;
	QCheckBox *mediaEndCheck_ = nullptr;
	QCheckBox *silenceCheck_ = nullptr;
	QSpinBox *silenceSpin_ = nullptr;
	QDoubleSpinBox *silenceThresholdSpin_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *elapsedLabel_ = nullptr;
	QLabel *motionLabel_ = nullptr;
	QLabel *mediaLabel_ = nullptr;
	QLabel *silenceLabel_ = nullptr;
	QLabel *combineHintLabel_ = nullptr;
	QComboBox *combineModeCombo_ = nullptr;
	QToolButton *openFolderButton_ = nullptr;
	QPushButton *minimizeButton_ = nullptr;
	QTimer *refreshTimer_ = nullptr;
	QTimer *saveTimer_ = nullptr;
	QLabel *warningLabel_ = nullptr;
	QLabel *envLabel_ = nullptr;

	bool settingsLoaded_ = false;
	int regionX_ = 0;
	int regionY_ = 0;
	int regionW_ = 100;
	int regionH_ = 100;
};
