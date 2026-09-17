#pragma once

#include <QWidget>

class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QTimer;

class RecordingMonitor;
class MotionDetector;
class StopController;

class AutoStopDock : public QWidget {
	Q_OBJECT

public:
	AutoStopDock(RecordingMonitor *monitor, MotionDetector *motion,
		     StopController *stop, QWidget *parent = nullptr);
	~AutoStopDock() override;

private slots:
	void onAutoStopToggled(bool enabled);
	void onMaxMinutesChanged(int minutes);
	void onMotionToggled(bool enabled);
	void onInactivitySecondsChanged(int seconds);
	void onSensitivityChanged(double percent);
	void onMinRecordingMinutesChanged(int minutes);
	void refreshStatus();

private:
	void loadSettings();
	void saveSettings() const;

	RecordingMonitor *monitor_ = nullptr;
	MotionDetector *motion_ = nullptr;
	StopController *stop_ = nullptr;

	QCheckBox *autoStopCheck_ = nullptr;
	QSpinBox *maxMinutesSpin_ = nullptr;
	QCheckBox *motionCheck_ = nullptr;
	QSpinBox *inactivitySpin_ = nullptr;
	QDoubleSpinBox *sensitivitySpin_ = nullptr;
	QSpinBox *minRecordingSpin_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *elapsedLabel_ = nullptr;
	QLabel *motionLabel_ = nullptr;
	QTimer *refreshTimer_ = nullptr;
};
