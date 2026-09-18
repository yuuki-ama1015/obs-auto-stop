#pragma once

#include <QWidget>

class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTimer;

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

private:
	void loadSettings();
	void saveSettings() const;
	void updateRegionControlsEnabled();
	void updateRegionStatusLabel();
	void applyMinRecordingToAll(int minutes);

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
	QTimer *refreshTimer_ = nullptr;

	int regionX_ = 0;
	int regionY_ = 0;
	int regionW_ = 100;
	int regionH_ = 100;
};
