#include "auto-stop-dock.hpp"

#include "recording-monitor.hpp"
#include "stop-controller.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>

namespace {
constexpr const char *kConfigSection = "ObsAutoStop";
constexpr const char *kKeyEnabled = "Enabled";
constexpr const char *kKeyMaxMinutes = "MaxRecordingMinutes";
constexpr int kDefaultMaxMinutes = 120;
} // namespace

AutoStopDock::AutoStopDock(RecordingMonitor *monitor, StopController *stop,
			   QWidget *parent)
	: QWidget(parent), monitor_(monitor), stop_(stop)
{
	setObjectName("obsAutoStopDock");

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	autoStopCheck_ = new QCheckBox(QStringLiteral("自動停止"), this);
	maxMinutesSpin_ = new QSpinBox(this);
	maxMinutesSpin_->setRange(0, 999);
	maxMinutesSpin_->setSuffix(QStringLiteral(" 分"));
	maxMinutesSpin_->setToolTip(
		QStringLiteral("0 にすると最大録画時間による自動停止は無効です"));

	auto *timerRow = new QWidget(this);
	auto *timerForm = new QFormLayout(timerRow);
	timerForm->setContentsMargins(0, 0, 0, 0);
	timerForm->addRow(QStringLiteral("最大録画時間"), maxMinutesSpin_);

	// Motion controls are shown for layout parity but disabled until MotionDetector lands.
	auto *motionCheck = new QCheckBox(QStringLiteral("静止検出"), this);
	motionCheck->setChecked(false);
	motionCheck->setEnabled(false);
	auto *inactivitySpin = new QSpinBox(this);
	inactivitySpin->setRange(1, 600);
	inactivitySpin->setValue(15);
	inactivitySpin->setSuffix(QStringLiteral(" 秒"));
	inactivitySpin->setEnabled(false);
	auto *sensitivitySpin = new QDoubleSpinBox(this);
	sensitivitySpin->setRange(0.1, 50.0);
	sensitivitySpin->setSingleStep(0.1);
	sensitivitySpin->setValue(2.0);
	sensitivitySpin->setSuffix(QStringLiteral(" %"));
	sensitivitySpin->setEnabled(false);
	auto *minRecSpin = new QSpinBox(this);
	minRecSpin->setRange(0, 120);
	minRecSpin->setValue(5);
	minRecSpin->setSuffix(QStringLiteral(" 分"));
	minRecSpin->setEnabled(false);

	auto *motionForm = new QFormLayout;
	motionForm->setContentsMargins(0, 0, 0, 0);
	motionForm->addRow(motionCheck);
	motionForm->addRow(QStringLiteral("静止判定時間"), inactivitySpin);
	motionForm->addRow(QStringLiteral("動き判定感度"), sensitivitySpin);
	motionForm->addRow(QStringLiteral("最低録画時間"), minRecSpin);

	statusLabel_ = new QLabel(QStringLiteral("状態: 待機中"), this);
	elapsedLabel_ = new QLabel(QStringLiteral("経過時間: 0 / — 秒"), this);

	layout->addWidget(autoStopCheck_);
	layout->addWidget(timerRow);
	layout->addSpacing(6);
	layout->addLayout(motionForm);
	layout->addSpacing(6);
	layout->addWidget(statusLabel_);
	layout->addWidget(elapsedLabel_);
	layout->addStretch(1);

	connect(autoStopCheck_, &QCheckBox::toggled, this,
		&AutoStopDock::onAutoStopToggled);
	connect(maxMinutesSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
		&AutoStopDock::onMaxMinutesChanged);

	refreshTimer_ = new QTimer(this);
	refreshTimer_->setInterval(500);
	connect(refreshTimer_, &QTimer::timeout, this,
		&AutoStopDock::refreshStatus);
	refreshTimer_->start();

	loadSettings();
	refreshStatus();
}

AutoStopDock::~AutoStopDock() = default;

void AutoStopDock::onAutoStopToggled(bool enabled)
{
	if (monitor_) {
		monitor_->setEnabled(enabled);
	}
	saveSettings();
	refreshStatus();
}

void AutoStopDock::onMaxMinutesChanged(int minutes)
{
	if (monitor_) {
		monitor_->setMaxRecordingDuration(std::chrono::seconds{
			static_cast<int64_t>(minutes) * 60});
	}
	saveSettings();
	refreshStatus();
}

void AutoStopDock::refreshStatus()
{
	if (!monitor_) {
		return;
	}

	const bool recording = monitor_->isRecording();
	const auto elapsed = monitor_->elapsedRecordingTime().count();
	const auto maxSec = monitor_->maxRecordingDuration().count();

	if (!monitor_->isEnabled()) {
		statusLabel_->setText(QStringLiteral("状態: 自動停止 OFF"));
	} else if (recording) {
		statusLabel_->setText(QStringLiteral("状態: 録画中"));
	} else if (stop_ && stop_->stopRequested()) {
		statusLabel_->setText(QStringLiteral("状態: 自動停止要求中"));
	} else {
		statusLabel_->setText(QStringLiteral("状態: 待機中"));
	}

	if (maxSec > 0) {
		elapsedLabel_->setText(
			QStringLiteral("経過時間: %1 / %2 秒")
				.arg(static_cast<qlonglong>(elapsed))
				.arg(static_cast<qlonglong>(maxSec)));
	} else {
		elapsedLabel_->setText(
			QStringLiteral("経過時間: %1 / — 秒")
				.arg(static_cast<qlonglong>(elapsed)));
	}
}

void AutoStopDock::loadSettings()
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config || !monitor_) {
		// Sensible defaults when config is unavailable.
		autoStopCheck_->setChecked(true);
		maxMinutesSpin_->setValue(kDefaultMaxMinutes);
		monitor_->setEnabled(true);
		monitor_->setMaxRecordingDuration(
			std::chrono::seconds{kDefaultMaxMinutes * 60});
		return;
	}

	config_set_default_bool(config, kConfigSection, kKeyEnabled, true);
	config_set_default_int(config, kConfigSection, kKeyMaxMinutes,
			       kDefaultMaxMinutes);

	const bool enabled = config_get_bool(config, kConfigSection, kKeyEnabled);
	int minutes = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyMaxMinutes));
	if (minutes < 0) {
		minutes = 0;
	}
	if (minutes > 999) {
		minutes = 999;
	}

	// Block signals while applying loaded values.
	const bool oldAuto = autoStopCheck_->blockSignals(true);
	const bool oldSpin = maxMinutesSpin_->blockSignals(true);
	autoStopCheck_->setChecked(enabled);
	maxMinutesSpin_->setValue(minutes);
	autoStopCheck_->blockSignals(oldAuto);
	maxMinutesSpin_->blockSignals(oldSpin);

	monitor_->setEnabled(enabled);
	monitor_->setMaxRecordingDuration(std::chrono::seconds{
		static_cast<int64_t>(minutes) * 60});
}

void AutoStopDock::saveSettings() const
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config) {
		return;
	}
	config_set_bool(config, kConfigSection, kKeyEnabled,
			autoStopCheck_->isChecked());
	config_set_int(config, kConfigSection, kKeyMaxMinutes,
		       maxMinutesSpin_->value());
	config_save_safe(config, "tmp", nullptr);
}
