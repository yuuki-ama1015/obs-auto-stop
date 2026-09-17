#pragma once

#include <QWidget>

class QCheckBox;
class QSpinBox;
class QLabel;
class QTimer;

class RecordingMonitor;
class StopController;

// OBS Dock UI for OBS Auto Stop (max recording timer + status).
class AutoStopDock : public QWidget {
	Q_OBJECT

public:
	AutoStopDock(RecordingMonitor *monitor, StopController *stop,
		     QWidget *parent = nullptr);
	~AutoStopDock() override;

private slots:
	void onAutoStopToggled(bool enabled);
	void onMaxMinutesChanged(int minutes);
	void refreshStatus();

private:
	void loadSettings();
	void saveSettings() const;

	RecordingMonitor *monitor_ = nullptr;
	StopController *stop_ = nullptr;

	QCheckBox *autoStopCheck_ = nullptr;
	QSpinBox *maxMinutesSpin_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *elapsedLabel_ = nullptr;
	QTimer *refreshTimer_ = nullptr;
};
