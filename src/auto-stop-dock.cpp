#include "auto-stop-dock.hpp"

#include "motion-detector.hpp"
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
constexpr const char *kKeyMotionEnabled = "MotionEnabled";
constexpr const char *kKeyInactivitySec = "InactivitySeconds";
constexpr const char *kKeySensitivity = "SensitivityPercent";
constexpr const char *kKeyMinRecordingMin = "MinRecordingMinutes";
constexpr int kDefaultMaxMinutes = 120;
constexpr int kDefaultInactivitySec = 15;
constexpr double kDefaultSensitivity = 2.0;
constexpr int kDefaultMinRecordingMin = 5;
} // namespace

AutoStopDock::AutoStopDock(RecordingMonitor *monitor, MotionDetector *motion,
			   StopController *stop, QWidget *parent)
	: QWidget(parent), monitor_(monitor), motion_(motion), stop_(stop)
{
	setObjectName("obsAutoStopDock");

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	autoStopCheck_ = new QCheckBox(QStringLiteral("録画の自動停止"), this);
	maxMinutesSpin_ = new QSpinBox(this);
	maxMinutesSpin_->setRange(0, 999);
	maxMinutesSpin_->setSuffix(QStringLiteral(" 分"));
	maxMinutesSpin_->setToolTip(
		QStringLiteral("0 にすると録画タイマーによる自動停止は無効です"));

	auto *timerForm = new QFormLayout;
	timerForm->setContentsMargins(0, 0, 0, 0);
	timerForm->addRow(QStringLiteral("録画タイマー"), maxMinutesSpin_);

	motionCheck_ = new QCheckBox(QStringLiteral("画面静止で録画終了"), this);
	inactivitySpin_ = new QSpinBox(this);
	inactivitySpin_->setRange(1, 600);
	inactivitySpin_->setSuffix(QStringLiteral(" 秒"));
	sensitivitySpin_ = new QDoubleSpinBox(this);
	sensitivitySpin_->setRange(0.1, 50.0);
	sensitivitySpin_->setSingleStep(0.1);
	sensitivitySpin_->setSuffix(QStringLiteral(" %"));
	minRecordingSpin_ = new QSpinBox(this);
	minRecordingSpin_->setRange(0, 120);
	minRecordingSpin_->setSuffix(QStringLiteral(" 分"));

	auto *motionForm = new QFormLayout;
	motionForm->setContentsMargins(0, 0, 0, 0);
	motionForm->addRow(motionCheck_);
	motionForm->addRow(QStringLiteral("静止と判断する時間"), inactivitySpin_);
	motionForm->addRow(QStringLiteral("静止判定の感度"), sensitivitySpin_);
	motionForm->addRow(QStringLiteral("最低録画時間"), minRecordingSpin_);

	statusLabel_ = new QLabel(QStringLiteral("プラグインステータス: 待機中"), this);
	elapsedLabel_ = new QLabel(QStringLiteral("経過時間: 0 / — 秒"), this);
	motionLabel_ = new QLabel(QStringLiteral("静止時間: 0 / — 秒"), this);

	layout->addWidget(autoStopCheck_);
	layout->addLayout(timerForm);
	layout->addSpacing(6);
	layout->addLayout(motionForm);
	layout->addSpacing(6);
	layout->addWidget(statusLabel_);
	layout->addWidget(elapsedLabel_);
	layout->addWidget(motionLabel_);
	layout->addStretch(1);

	connect(autoStopCheck_, &QCheckBox::toggled, this,
		&AutoStopDock::onAutoStopToggled);
	connect(maxMinutesSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
		&AutoStopDock::onMaxMinutesChanged);
	connect(motionCheck_, &QCheckBox::toggled, this,
		&AutoStopDock::onMotionToggled);
	connect(inactivitySpin_, qOverload<int>(&QSpinBox::valueChanged), this,
		&AutoStopDock::onInactivitySecondsChanged);
	connect(sensitivitySpin_, qOverload<double>(&QDoubleSpinBox::valueChanged),
		this, &AutoStopDock::onSensitivityChanged);
	connect(minRecordingSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
		&AutoStopDock::onMinRecordingMinutesChanged);

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
}

void AutoStopDock::onMaxMinutesChanged(int minutes)
{
	if (monitor_) {
		monitor_->setMaxRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minutes) * 60});
	}
	saveSettings();
}

void AutoStopDock::onMotionToggled(bool enabled)
{
	if (motion_) {
		motion_->setEnabled(enabled);
	}
	saveSettings();
}

void AutoStopDock::onInactivitySecondsChanged(int seconds)
{
	if (motion_) {
		motion_->setInactivityDuration(std::chrono::seconds{seconds});
	}
	saveSettings();
}

void AutoStopDock::onSensitivityChanged(double percent)
{
	if (motion_) {
		motion_->setSensitivityPercent(percent);
	}
	saveSettings();
}

void AutoStopDock::onMinRecordingMinutesChanged(int minutes)
{
	if (motion_) {
		motion_->setMinimumRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minutes) * 60});
	}
	saveSettings();
}

void AutoStopDock::refreshStatus()
{
	if (!monitor_) {
		return;
	}

	const auto elapsed = monitor_->elapsedRecordingTime().count();
	const auto maxSec = monitor_->maxRecordingDuration().count();

	if (!monitor_->isEnabled()) {
		statusLabel_->setText(QStringLiteral("プラグインステータス: 録画の自動停止 OFF"));
	} else if (monitor_->isRecording()) {
		statusLabel_->setText(QStringLiteral("プラグインステータス: 録画中"));
	} else if (stop_ && stop_->stopRequested()) {
		statusLabel_->setText(QStringLiteral("プラグインステータス: 自動停止要求中"));
	} else {
		statusLabel_->setText(QStringLiteral("プラグインステータス: 待機中"));
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

	if (motion_ && motion_->isEnabled()) {
		const auto still = motion_->stillnessDuration().count();
		const auto need = motion_->inactivityDuration().count();
		motionLabel_->setText(
			QStringLiteral("静止時間: %1 / %2 秒 (動き %.2f%%)")
				.arg(static_cast<qlonglong>(still))
				.arg(static_cast<qlonglong>(need))
				.arg(motion_->lastMotionPercent(), 0, 'f', 2));
	} else {
		motionLabel_->setText(QStringLiteral("静止時間: —"));
	}
}

void AutoStopDock::loadSettings()
{
	config_t *config = obs_frontend_get_profile_config();

	auto applyDefaults = [&]() {
		autoStopCheck_->setChecked(true);
		maxMinutesSpin_->setValue(kDefaultMaxMinutes);
		motionCheck_->setChecked(false);
		inactivitySpin_->setValue(kDefaultInactivitySec);
		sensitivitySpin_->setValue(kDefaultSensitivity);
		minRecordingSpin_->setValue(kDefaultMinRecordingMin);
		if (monitor_) {
			monitor_->setEnabled(true);
			monitor_->setMaxRecordingDuration(
				std::chrono::seconds{kDefaultMaxMinutes * 60});
		}
		if (motion_) {
			motion_->setEnabled(false);
			motion_->setInactivityDuration(
				std::chrono::seconds{kDefaultInactivitySec});
			motion_->setSensitivityPercent(kDefaultSensitivity);
			motion_->setMinimumRecordingDuration(
				std::chrono::seconds{kDefaultMinRecordingMin *
						     60});
		}
	};

	if (!config) {
		applyDefaults();
		return;
	}

	config_set_default_bool(config, kConfigSection, kKeyEnabled, true);
	config_set_default_int(config, kConfigSection, kKeyMaxMinutes,
			       kDefaultMaxMinutes);
	config_set_default_bool(config, kConfigSection, kKeyMotionEnabled,
				false);
	config_set_default_int(config, kConfigSection, kKeyInactivitySec,
			       kDefaultInactivitySec);
	config_set_default_double(config, kConfigSection, kKeySensitivity,
				  kDefaultSensitivity);
	config_set_default_int(config, kConfigSection, kKeyMinRecordingMin,
			       kDefaultMinRecordingMin);

	const bool enabled = config_get_bool(config, kConfigSection, kKeyEnabled);
	int minutes = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyMaxMinutes));
	const bool motionEnabled =
		config_get_bool(config, kConfigSection, kKeyMotionEnabled);
	int inactivity = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyInactivitySec));
	const double sensitivity =
		config_get_double(config, kConfigSection, kKeySensitivity);
	int minRecMin = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyMinRecordingMin));

	if (minutes < 0)
		minutes = 0;
	if (minutes > 999)
		minutes = 999;
	if (inactivity < 1)
		inactivity = 1;
	if (minRecMin < 0)
		minRecMin = 0;

	const bool old1 = autoStopCheck_->blockSignals(true);
	const bool old2 = maxMinutesSpin_->blockSignals(true);
	const bool old3 = motionCheck_->blockSignals(true);
	const bool old4 = inactivitySpin_->blockSignals(true);
	const bool old5 = sensitivitySpin_->blockSignals(true);
	const bool old6 = minRecordingSpin_->blockSignals(true);

	autoStopCheck_->setChecked(enabled);
	maxMinutesSpin_->setValue(minutes);
	motionCheck_->setChecked(motionEnabled);
	inactivitySpin_->setValue(inactivity);
	sensitivitySpin_->setValue(sensitivity);
	minRecordingSpin_->setValue(minRecMin);

	autoStopCheck_->blockSignals(old1);
	maxMinutesSpin_->blockSignals(old2);
	motionCheck_->blockSignals(old3);
	inactivitySpin_->blockSignals(old4);
	sensitivitySpin_->blockSignals(old5);
	minRecordingSpin_->blockSignals(old6);

	if (monitor_) {
		monitor_->setEnabled(enabled);
		monitor_->setMaxRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minutes) * 60});
	}
	if (motion_) {
		motion_->setEnabled(motionEnabled);
		motion_->setInactivityDuration(
			std::chrono::seconds{inactivity});
		motion_->setSensitivityPercent(sensitivity);
		motion_->setMinimumRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minRecMin) *
					     60});
	}
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
	config_set_bool(config, kConfigSection, kKeyMotionEnabled,
			motionCheck_->isChecked());
	config_set_int(config, kConfigSection, kKeyInactivitySec,
		       inactivitySpin_->value());
	config_set_double(config, kConfigSection, kKeySensitivity,
			  sensitivitySpin_->value());
	config_set_int(config, kConfigSection, kKeyMinRecordingMin,
		       minRecordingSpin_->value());
	config_save_safe(config, "tmp", nullptr);
}
