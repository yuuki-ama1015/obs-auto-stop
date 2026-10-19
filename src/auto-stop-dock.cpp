#include "auto-stop-dock.hpp"
#include "plugin-text.hpp"

#include "motion-detector.hpp"
#include "media-end-watcher.hpp"
#include "silence-detector.hpp"
#include "recording-monitor.hpp"
#include "region-select-dialog.hpp"
#include "stop-controller.hpp"

#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <util/config-file.h>

#include <QCheckBox>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QDesktopServices>
#include <QStyle>
#include <QToolButton>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QPushButton>
#include <QShowEvent>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

namespace {
constexpr const char *kConfigSection = "ObsAutoStop";
constexpr const char *kKeyEnabled = "Enabled";
constexpr const char *kKeyMaxMinutes = "MaxRecordingMinutes";
constexpr const char *kKeyMotionEnabled = "MotionEnabled";
constexpr const char *kKeyInactivitySec = "InactivitySeconds";
constexpr const char *kKeySensitivity = "SensitivityPercent";
constexpr const char *kKeyMinRecordingMin = "MinRecordingMinutes";
constexpr const char *kKeyRegionEnabled = "RegionEnabled";
constexpr const char *kKeyRegionX = "RegionXPercent";
constexpr const char *kKeyRegionY = "RegionYPercent";
constexpr const char *kKeyRegionW = "RegionWPercent";
constexpr const char *kKeyRegionH = "RegionHPercent";
constexpr const char *kKeyMediaEndEnabled = "MediaEndEnabled";
constexpr const char *kKeySilenceEnabled = "SilenceEnabled";
constexpr const char *kKeySilenceSec = "SilenceSeconds";
constexpr const char *kKeySilenceThresholdDb = "SilenceThresholdDb";
constexpr const char *kKeyCombineMode = "CombineMode";
constexpr int kDefaultMaxMinutes = 120;
constexpr int kDefaultInactivitySec = 15;
constexpr double kDefaultSensitivity = 0.5;
constexpr int kDefaultMinRecordingMin = 5;
constexpr int kDefaultSilenceSec = 15;
constexpr double kDefaultSilenceThresholdDb = -50.0;
} // namespace

AutoStopDock::AutoStopDock(RecordingMonitor *monitor, MotionDetector *motion,
			   MediaEndWatcher *media, SilenceDetector *silence,
			   StopController *stop, QWidget *parent)
	: QWidget(parent),
	  monitor_(monitor),
	  motion_(motion),
	  media_(media),
	  silence_(silence),
	  stop_(stop)
{
	setObjectName("obsAutoStopDock");

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	combineModeCombo_ = new QComboBox(this);
	combineModeCombo_->addItem(ObsText("Dock.Combine.Or"),
				   static_cast<int>(StopCombineMode::Or));
	combineModeCombo_->addItem(ObsText("Dock.Combine.And"),
				   static_cast<int>(StopCombineMode::And));
	combineModeCombo_->setToolTip(
		ObsText("Dock.Combine.Tooltip"));

	openFolderButton_ = new QToolButton(this);
	openFolderButton_->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
	openFolderButton_->setAutoRaise(true);
	openFolderButton_->setToolTip(ObsText("Dock.OpenFolder.Tooltip"));

	combineHintLabel_ = new QLabel(this);
	combineHintLabel_->setWordWrap(true);
	combineHintLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	combineHintLabel_->setStyleSheet(
		QStringLiteral("color: palette(mid); margin: 0; padding: 0;"));

	autoStopCheck_ = new QCheckBox(ObsText("Dock.Timer.Check"), this);
	maxMinutesSpin_ = new QSpinBox(this);
	maxMinutesSpin_->setRange(0, 999);
	maxMinutesSpin_->setSuffix(ObsText("Dock.Suffix.Minutes"));
	maxMinutesSpin_->setToolTip(
		ObsText("Dock.Timer.Tooltip"));

	auto *timerForm = new QFormLayout;
	timerForm->setContentsMargins(0, 0, 0, 0);
	timerForm->addRow(ObsText("Dock.Timer.Label"), maxMinutesSpin_);

	motionCheck_ = new QCheckBox(ObsText("Dock.Motion.Check"), this);
	inactivitySpin_ = new QSpinBox(this);
	inactivitySpin_->setRange(1, 600);
	inactivitySpin_->setSuffix(ObsText("Dock.Suffix.Seconds"));
	sensitivitySpin_ = new QDoubleSpinBox(this);
	sensitivitySpin_->setRange(0.1, 50.0);
	sensitivitySpin_->setSingleStep(0.1);
	sensitivitySpin_->setSuffix(QStringLiteral(" %"));
	sensitivitySpin_->setToolTip(ObsText("Dock.Motion.Sensitivity.Tooltip"));
	minRecordingSpin_ = new QSpinBox(this);
	minRecordingSpin_->setRange(0, 120);
	minRecordingSpin_->setSuffix(ObsText("Dock.Suffix.Minutes"));

	auto *motionForm = new QFormLayout;
	motionForm->setContentsMargins(0, 0, 0, 0);
	motionForm->addRow(motionCheck_);
	motionForm->addRow(ObsText("Dock.Motion.Duration"), inactivitySpin_);
	motionForm->addRow(ObsText("Dock.Motion.Sensitivity"), sensitivitySpin_);
	motionForm->addRow(ObsText("Dock.Motion.MinRecording"), minRecordingSpin_);

	regionCheck_ = new QCheckBox(
		ObsText("Dock.Region.Check"), this);
	regionCheck_->setToolTip(ObsText("Dock.Region.Tooltip"));
	selectRegionButton_ =
		new QPushButton(ObsText("Dock.Region.Select"), this);
	regionStatusLabel_ = new QLabel(this);
	regionStatusLabel_->setWordWrap(true);

	auto *regionInner = new QVBoxLayout;
	regionInner->setContentsMargins(0, 0, 0, 0);
	regionInner->setSpacing(4);
	regionInner->addWidget(selectRegionButton_);
	regionInner->addWidget(regionStatusLabel_);

	auto *regionControls = new QWidget(this);
	regionControls->setLayout(regionInner);

	auto *regionIndent = new QHBoxLayout;
	regionIndent->setContentsMargins(24, 0, 0, 0);
	regionIndent->setSpacing(4);
	auto *regionColumn = new QVBoxLayout;
	regionColumn->setContentsMargins(0, 0, 0, 0);
	regionColumn->setSpacing(4);
	regionColumn->addWidget(regionCheck_);
	regionColumn->addWidget(regionControls);
	regionIndent->addLayout(regionColumn);

	regionWidget_ = new QWidget(this);
	regionWidget_->setLayout(regionIndent);

	mediaEndCheck_ = new QCheckBox(
		ObsText("Dock.Media.Check"), this);
	mediaEndCheck_->setToolTip(ObsText("Dock.Media.Tooltip"));

	silenceCheck_ = new QCheckBox(
		ObsText("Dock.Silence.Check"), this);
	silenceSpin_ = new QSpinBox(this);
	silenceSpin_->setRange(1, 600);
	silenceSpin_->setSuffix(ObsText("Dock.Suffix.Seconds"));
	silenceThresholdSpin_ = new QDoubleSpinBox(this);
	silenceThresholdSpin_->setRange(-100.0, 0.0);
	silenceThresholdSpin_->setSingleStep(1.0);
	silenceThresholdSpin_->setSuffix(QStringLiteral(" dB"));
	silenceThresholdSpin_->setToolTip(
		ObsText("Dock.Silence.Threshold.Tooltip"));

	auto *silenceForm = new QFormLayout;
	silenceForm->setContentsMargins(0, 0, 0, 0);
	silenceForm->addRow(silenceCheck_);
	silenceForm->addRow(ObsText("Dock.Silence.Duration"), silenceSpin_);
	silenceForm->addRow(ObsText("Dock.Silence.Threshold"), silenceThresholdSpin_);

	statusLabel_ = new QLabel(ObsText("Dock.Status.Idle"), this);
	elapsedLabel_ = new QLabel(ObsText("Dock.Elapsed.Initial"), this);
	motionLabel_ = new QLabel(ObsText("Dock.Still.Initial"), this);
	mediaLabel_ = new QLabel(ObsText("Dock.Media.Off"), this);
	silenceLabel_ = new QLabel(ObsText("Dock.Silence.Off"), this);
	warningLabel_ = new QLabel(this);
	warningLabel_->setWordWrap(true);
	warningLabel_->setStyleSheet(QStringLiteral("color: #e0a030;"));
	warningLabel_->hide();
	envLabel_ = new QLabel(this);
	envLabel_->setWordWrap(true);
	envLabel_->setStyleSheet(QStringLiteral("color: palette(mid);"));
	envLabel_->hide();

	// Order: combine mode → hint → media end → timer → motion → silence → status → minimize
	auto *combineRow = new QHBoxLayout;
	combineRow->setContentsMargins(0, 0, 0, 0);
	combineRow->setSpacing(6);
	combineRow->addWidget(new QLabel(ObsText("Dock.Combine.Label"), this));
	combineRow->addWidget(combineModeCombo_, 1);
	combineRow->addWidget(openFolderButton_);

	auto *combineBox = new QVBoxLayout;
	combineBox->setContentsMargins(0, 0, 0, 0);
	combineBox->setSpacing(2);
	combineBox->addLayout(combineRow);
	combineBox->addWidget(combineHintLabel_);
	layout->addLayout(combineBox);
	layout->addWidget(mediaEndCheck_);
	layout->addSpacing(6);
	layout->addWidget(autoStopCheck_);
	layout->addLayout(timerForm);
	layout->addSpacing(6);
	layout->addLayout(motionForm);
	layout->addWidget(regionWidget_);
	layout->addSpacing(6);
	layout->addLayout(silenceForm);
	layout->addSpacing(6);
	layout->addWidget(statusLabel_);
	layout->addWidget(elapsedLabel_);
	layout->addWidget(motionLabel_);
	layout->addWidget(mediaLabel_);
	layout->addWidget(silenceLabel_);
	layout->addWidget(warningLabel_);
	layout->addWidget(envLabel_);

	minimizeButton_ = new QPushButton(ObsText("Dock.Minimize"), this);
	minimizeButton_->setToolTip(ObsText("Dock.Minimize.Tooltip"));
	layout->addWidget(minimizeButton_);
	layout->addStretch(1);

	connect(autoStopCheck_, &QCheckBox::toggled, this,
		&AutoStopDock::onAutoStopToggled);
	connect(maxMinutesSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
		&AutoStopDock::onMaxMinutesChanged);
	connect(motionCheck_, &QCheckBox::toggled, this,
		&AutoStopDock::onMotionToggled);
	connect(inactivitySpin_, qOverload<int>(&QSpinBox::valueChanged), this,
		&AutoStopDock::onInactivitySecondsChanged);
	connect(sensitivitySpin_,
		qOverload<double>(&QDoubleSpinBox::valueChanged), this,
		&AutoStopDock::onSensitivityChanged);
	connect(minRecordingSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
		&AutoStopDock::onMinRecordingMinutesChanged);
	connect(regionCheck_, &QCheckBox::toggled, this,
		&AutoStopDock::onRegionToggled);
	connect(selectRegionButton_, &QPushButton::clicked, this,
		&AutoStopDock::onSelectRegionClicked);
	connect(mediaEndCheck_, &QCheckBox::toggled, this,
		&AutoStopDock::onMediaEndToggled);
	connect(silenceCheck_, &QCheckBox::toggled, this,
		&AutoStopDock::onSilenceToggled);
	connect(silenceSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
		&AutoStopDock::onSilenceSecondsChanged);
	connect(silenceThresholdSpin_,
		qOverload<double>(&QDoubleSpinBox::valueChanged), this,
		&AutoStopDock::onSilenceThresholdChanged);
	connect(minimizeButton_, &QPushButton::clicked, this,
		&AutoStopDock::onMinimizeToTaskbar);
	connect(combineModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
		this, &AutoStopDock::onCombineModeChanged);
	connect(openFolderButton_, &QToolButton::clicked, this,
		&AutoStopDock::onOpenRecordingFolder);


	refreshTimer_ = new QTimer(this);
	refreshTimer_->setInterval(500);
	connect(refreshTimer_, &QTimer::timeout, this,
		&AutoStopDock::refreshStatus);
	refreshTimer_->start();

	// Coalesce rapid edits (spin box auto-repeat, typing) into one write.
	saveTimer_ = new QTimer(this);
	saveTimer_->setSingleShot(true);
	saveTimer_->setInterval(500);
	connect(saveTimer_, &QTimer::timeout, this,
		&AutoStopDock::saveSettingsNow);

	loadSettings();
	refreshStatus();
}

AutoStopDock::~AutoStopDock() = default;

void AutoStopDock::scheduleSave()
{
	saveTimer_->start();
}

void AutoStopDock::flushPendingSave()
{
	if (saveTimer_ && saveTimer_->isActive()) {
		saveTimer_->stop();
		saveSettingsNow();
	}
}

void AutoStopDock::setEnvOverrideActive(bool active)
{
	envLabel_->setText(ObsText("Dock.EnvOverride"));
	envLabel_->setVisible(active);
}

void AutoStopDock::onAutoStopToggled(bool enabled)
{
	if (monitor_) {
		monitor_->setEnabled(enabled);
	}
	scheduleSave();
}

void AutoStopDock::onMaxMinutesChanged(int minutes)
{
	if (monitor_) {
		monitor_->setMaxRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minutes) * 60});
	}
	scheduleSave();
}

void AutoStopDock::onMotionToggled(bool enabled)
{
	if (motion_) {
		motion_->setEnabled(enabled);
	}
	updateRegionControlsEnabled();
	scheduleSave();
}

void AutoStopDock::onInactivitySecondsChanged(int seconds)
{
	if (motion_) {
		motion_->setInactivityDuration(std::chrono::seconds{seconds});
	}
	scheduleSave();
}

void AutoStopDock::onSensitivityChanged(double percent)
{
	// Sensitivity is session-only: never written to the profile.
	if (motion_) {
		motion_->setSensitivityPercent(percent);
	}
}

void AutoStopDock::applyMinRecordingToAll(int minutes)
{
	const auto dur =
		std::chrono::seconds{static_cast<int64_t>(minutes) * 60};
	if (motion_) {
		motion_->setMinimumRecordingDuration(dur);
	}
	if (media_) {
		media_->setMinimumRecordingDuration(dur);
	}
	if (silence_) {
		silence_->setMinimumRecordingDuration(dur);
	}
}

void AutoStopDock::onMinRecordingMinutesChanged(int minutes)
{
	applyMinRecordingToAll(minutes);
	scheduleSave();
}

void AutoStopDock::onMediaEndToggled(bool enabled)
{
	if (media_) {
		media_->setEnabled(enabled);
	}
	scheduleSave();
}

void AutoStopDock::onSilenceToggled(bool enabled)
{
	if (silence_) {
		silence_->setEnabled(enabled);
	}
	scheduleSave();
}

void AutoStopDock::onSilenceSecondsChanged(int seconds)
{
	if (silence_) {
		silence_->setSilenceDuration(std::chrono::seconds{seconds});
	}
	scheduleSave();
}

void AutoStopDock::onSilenceThresholdChanged(double db)
{
	if (silence_) {
		silence_->setThresholdDb(db);
	}
	scheduleSave();
}

void AutoStopDock::onRegionToggled(bool enabled)
{
	if (motion_) {
		motion_->setRegionEnabled(enabled);
	}
	updateRegionControlsEnabled();
	scheduleSave();
}

void AutoStopDock::onSelectRegionClicked()
{
	QWidget *parent = this;
	if (void *mainWin = obs_frontend_get_main_window()) {
		parent = static_cast<QWidget *>(mainWin);
	}

	RegionSelectDialog dialog(parent, regionX_, regionY_, regionW_,
				  regionH_);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}

	regionX_ = dialog.regionX();
	regionY_ = dialog.regionY();
	regionW_ = dialog.regionW();
	regionH_ = dialog.regionH();

	if (motion_) {
		motion_->setRegionPercent(regionX_, regionY_, regionW_,
					  regionH_);
		motion_->regionPercent(regionX_, regionY_, regionW_, regionH_);
	}

	updateRegionStatusLabel();
	scheduleSave();
}

void AutoStopDock::updateRegionControlsEnabled()
{
	const bool motionOn = motionCheck_ && motionCheck_->isChecked();
	const bool regionOn = regionCheck_ && regionCheck_->isChecked();
	// Region is a sub-option of stillness detection.
	regionCheck_->setEnabled(motionOn);
	const bool controlsOn = motionOn && regionOn;
	selectRegionButton_->setEnabled(controlsOn);
	regionStatusLabel_->setEnabled(controlsOn);
}

void AutoStopDock::updateRegionStatusLabel()
{
	regionStatusLabel_->setText(
		ObsText("Dock.Region.Status")
			.arg(regionX_)
			.arg(regionY_)
			.arg(regionW_)
			.arg(regionH_));
}

void AutoStopDock::refreshStatus()
{
	if (!monitor_ || !isVisible()) {
		return;
	}

	const auto elapsed = monitor_->elapsedRecordingTime().count();
	const auto maxSec = monitor_->maxRecordingDuration().count();

	const bool anyOn = monitor_->isEnabled() ||
			   (motion_ && motion_->isEnabled()) ||
			   (media_ && media_->isEnabled()) ||
			   (silence_ && silence_->isEnabled());
	if (!anyOn) {
		statusLabel_->setText(
			ObsText("Dock.Status.Off"));
	} else if (monitor_->isPaused()) {
		statusLabel_->setText(
			ObsText("Dock.Status.Paused"));
	} else if (stop_ && stop_->stopRequested()) {
		statusLabel_->setText(
			ObsText("Dock.Status.Stopping"));
	} else if (monitor_->isRecording()) {
		statusLabel_->setText(ObsText("Dock.Status.Recording"));
	} else {
		statusLabel_->setText(ObsText("Dock.Status.Idle"));
	}

	if (monitor_->isEnabled() && maxSec > 0) {
		elapsedLabel_->setText(
			ObsText("Dock.Elapsed")
				.arg(static_cast<qlonglong>(elapsed))
				.arg(static_cast<qlonglong>(maxSec)));
	} else {
		elapsedLabel_->setText(
			ObsText("Dock.Elapsed.NoLimit")
				.arg(static_cast<qlonglong>(elapsed)));
	}

	if (motion_ && motion_->isEnabled()) {
		const auto still = motion_->stillnessDuration().count();
		const auto need = motion_->inactivityDuration().count();
		QString regionNote;
		if (motion_->isRegionEnabled()) {
			int x, y, w, h;
			motion_->regionPercent(x, y, w, h);
			regionNote = ObsText("Dock.Still.RegionNote")
					     .arg(x)
					     .arg(y)
					     .arg(w)
					     .arg(h);
		}
		motionLabel_->setText(
			ObsText("Dock.Still")
				.arg(static_cast<qlonglong>(still))
				.arg(static_cast<qlonglong>(need))
				.arg(motion_->lastMotionPercent(), 0, 'f', 2)
				.arg(regionNote));
	} else {
		motionLabel_->setText(ObsText("Dock.Still.Off"));
	}

	if (media_ && media_->isEnabled()) {
		const auto name = media_->endedSourceName();
		if (!name.empty()) {
			mediaLabel_->setText(
				ObsText("Dock.Media.Detected")
					.arg(QString::fromStdString(name)));
		} else if (media_->isMonitoring()) {
			mediaLabel_->setText(
				ObsText("Dock.Media.Watching")
					.arg(static_cast<qulonglong>(media_->watchedCount())));
		} else {
			mediaLabel_->setText(ObsText("Dock.Media.WaitRecording"));
		}
	} else {
		mediaLabel_->setText(ObsText("Dock.Media.Off"));
	}

	if (silence_ && silence_->isEnabled()) {
		const auto still = silence_->silenceActiveDuration().count();
		const auto need = silence_->silenceDuration().count();
		silenceLabel_->setText(
			ObsText("Dock.Silence")
				.arg(static_cast<qlonglong>(still))
				.arg(static_cast<qlonglong>(need))
				.arg(silence_->lastLevelDb(), 0, 'f', 1));
	} else {
		silenceLabel_->setText(ObsText("Dock.Silence.Off"));
	}

	updateAndWarning();
}

void AutoStopDock::updateAndWarning()
{
	// AND needs every enabled condition; media-end can never be met when
	// no (non-looping) media source is being watched.
	const bool andMode = stop_ && stop_->combineMode() == StopCombineMode::And;
	const bool unsatisfiable = andMode && media_ && media_->isEnabled() &&
				   media_->isMonitoring() &&
				   media_->watchedCount() == 0;
	if (unsatisfiable) {
		warningLabel_->setText(ObsText("Dock.Warning.AndMedia"));
	}
	warningLabel_->setVisible(unsatisfiable);
}

namespace {
template<typename T> T clampTo(T v, T lo, T hi)
{
	return std::max(lo, std::min(hi, v));
}
} // namespace

AutoStopDock::Settings AutoStopDock::readSettings()
{
	Settings s;
	config_t *config = obs_frontend_get_profile_config();
	if (!config) {
		return s;
	}
	auto readBool = [&](const char *key, bool def) {
		config_set_default_bool(config, kConfigSection, key, def);
		return config_get_bool(config, kConfigSection, key);
	};
	auto readInt = [&](const char *key, int def, int lo, int hi) {
		config_set_default_int(config, kConfigSection, key, def);
		const int64_t v = config_get_int(config, kConfigSection, key);
		return static_cast<int>(clampTo<int64_t>(v, lo, hi));
	};

	s.timerEnabled = readBool(kKeyEnabled, true);
	s.maxMinutes = readInt(kKeyMaxMinutes, kDefaultMaxMinutes, 0, 999);
	s.motionEnabled = readBool(kKeyMotionEnabled, false);
	s.inactivitySec = readInt(kKeyInactivitySec, kDefaultInactivitySec, 1, 600);
	s.minRecordingMin = readInt(kKeyMinRecordingMin, kDefaultMinRecordingMin, 0, 120);
	s.regionEnabled = readBool(kKeyRegionEnabled, false);
	s.regionX = readInt(kKeyRegionX, 0, 0, 99);
	s.regionY = readInt(kKeyRegionY, 0, 0, 99);
	s.regionW = readInt(kKeyRegionW, 100, 1, 100);
	s.regionH = readInt(kKeyRegionH, 100, 1, 100);
	s.mediaEndEnabled = readBool(kKeyMediaEndEnabled, false);
	s.combineMode = readInt(kKeyCombineMode, 0, 0, 1);
	s.silenceEnabled = readBool(kKeySilenceEnabled, false);
	s.silenceSec = readInt(kKeySilenceSec, kDefaultSilenceSec, 1, 600);
	config_set_default_double(config, kConfigSection, kKeySilenceThresholdDb,
				  kDefaultSilenceThresholdDb);
	s.silenceDb = clampTo(config_get_double(config, kConfigSection,
						kKeySilenceThresholdDb),
			      -100.0, 0.0);
	// Stillness sensitivity is session-only and always starts at default.
	return s;
}

void AutoStopDock::applySettingsToUi(const Settings &s)
{
	const QSignalBlocker b1(autoStopCheck_), b2(maxMinutesSpin_),
		b3(motionCheck_), b4(inactivitySpin_), b5(sensitivitySpin_),
		b6(minRecordingSpin_), b7(regionCheck_), b8(mediaEndCheck_),
		b9(combineModeCombo_), b10(silenceCheck_), b11(silenceSpin_),
		b12(silenceThresholdSpin_);

	autoStopCheck_->setChecked(s.timerEnabled);
	maxMinutesSpin_->setValue(s.maxMinutes);
	motionCheck_->setChecked(s.motionEnabled);
	inactivitySpin_->setValue(s.inactivitySec);
	if (!settingsLoaded_) {
		// Session-only: start at default, keep the value across profiles.
		sensitivitySpin_->setValue(kDefaultSensitivity);
	}
	minRecordingSpin_->setValue(s.minRecordingMin);
	regionCheck_->setChecked(s.regionEnabled);
	mediaEndCheck_->setChecked(s.mediaEndEnabled);
	const int combineIdx = combineModeCombo_->findData(s.combineMode);
	combineModeCombo_->setCurrentIndex(combineIdx >= 0 ? combineIdx : 0);
	silenceCheck_->setChecked(s.silenceEnabled);
	silenceSpin_->setValue(s.silenceSec);
	silenceThresholdSpin_->setValue(s.silenceDb);
}

void AutoStopDock::applySettingsToModel(const Settings &s)
{
	const auto minRec =
		std::chrono::seconds{static_cast<int64_t>(s.minRecordingMin) * 60};
	if (monitor_) {
		monitor_->setEnabled(s.timerEnabled);
		monitor_->setMaxRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(s.maxMinutes) * 60});
	}
	if (stop_) {
		stop_->setCombineMode(static_cast<StopCombineMode>(s.combineMode));
	}
	if (motion_) {
		motion_->setInactivityDuration(std::chrono::seconds{s.inactivitySec});
		motion_->setSensitivityPercent(sensitivitySpin_->value());
		motion_->setMinimumRecordingDuration(minRec);
		motion_->setRegionEnabled(s.regionEnabled);
		motion_->setRegionPercent(s.regionX, s.regionY, s.regionW, s.regionH);
		motion_->regionPercent(regionX_, regionY_, regionW_, regionH_);
		motion_->setEnabled(s.motionEnabled);
	} else {
		regionX_ = s.regionX;
		regionY_ = s.regionY;
		regionW_ = s.regionW;
		regionH_ = s.regionH;
	}
	if (media_) {
		media_->setMinimumRecordingDuration(minRec);
		media_->setEnabled(s.mediaEndEnabled);
	}
	if (silence_) {
		silence_->setSilenceDuration(std::chrono::seconds{s.silenceSec});
		silence_->setThresholdDb(s.silenceDb);
		silence_->setMinimumRecordingDuration(minRec);
		silence_->setEnabled(s.silenceEnabled);
	}
}

void AutoStopDock::loadSettings()
{
	if (saveTimer_) {
		saveTimer_->stop(); // never write old values into a new profile
	}
	const Settings s = readSettings();
	applySettingsToUi(s);
	applySettingsToModel(s);
	settingsLoaded_ = true;
	updateRegionStatusLabel();
	updateRegionControlsEnabled();
	updateCombineHint();
}

void AutoStopDock::saveSettingsNow()
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
	// Sensitivity is session-only: drop any value written by old versions.
	config_remove_value(config, kConfigSection, kKeySensitivity);
	config_set_int(config, kConfigSection, kKeyMinRecordingMin,
		       minRecordingSpin_->value());
	config_set_bool(config, kConfigSection, kKeyRegionEnabled,
			regionCheck_->isChecked());
	config_set_int(config, kConfigSection, kKeyRegionX, regionX_);
	config_set_int(config, kConfigSection, kKeyRegionY, regionY_);
	config_set_int(config, kConfigSection, kKeyRegionW, regionW_);
	config_set_int(config, kConfigSection, kKeyRegionH, regionH_);
	config_set_bool(config, kConfigSection, kKeyMediaEndEnabled,
			mediaEndCheck_->isChecked());
	config_set_int(config, kConfigSection, kKeyCombineMode,
		       combineModeCombo_->currentData().toInt());
	config_set_bool(config, kConfigSection, kKeySilenceEnabled,
			silenceCheck_->isChecked());
	config_set_int(config, kConfigSection, kKeySilenceSec,
		       silenceSpin_->value());
	config_set_double(config, kConfigSection, kKeySilenceThresholdDb,
			  silenceThresholdSpin_->value());
	config_save_safe(config, "tmp", nullptr);
}

QDockWidget *AutoStopDock::hostDock() const
{
	// OBS wraps the widget in a QDockWidget. Never fall through to the
	// main window: minimizing it would minimize all of OBS.
	for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
		if (auto *dock = qobject_cast<QDockWidget *>(w)) {
			return dock;
		}
	}
	return nullptr;
}

void AutoStopDock::ensureFloatingMinimizeButton()
{
	QDockWidget *dock = hostDock();
	if (!dock || !dock->isFloating()) {
		return;
	}
	const Qt::WindowFlags flags = dock->windowFlags();
	// Qt::Tool already contains the Qt::Window bit. Replace the type rather
	// than OR-ing it, otherwise Windows minimizes the dock to a desktop bar.
	const Qt::WindowFlags floatingFlags =
		(flags & ~Qt::WindowType_Mask) | Qt::Window |
		Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint;
	const bool changed = flags != floatingFlags;
	if (changed) {
		dock->setWindowFlags(floatingFlags);
	}
#ifdef Q_OS_WIN
	// Keep the OBS parent for docking/lifetime, but give the owned floating
	// window its own taskbar entry. A normal owned window alone has none.
	const HWND hwnd = reinterpret_cast<HWND>(dock->winId());
	const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	const LONG_PTR taskbarStyle = (style & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW;
	if (style != taskbarStyle) {
		SetWindowLongPtrW(hwnd, GWL_EXSTYLE, taskbarStyle);
	}
#endif
	if (changed) {
		dock->show();
	}
}

void AutoStopDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	ensureFloatingMinimizeButton();
}

void AutoStopDock::onMinimizeToTaskbar()
{
	QDockWidget *dock = hostDock();
	if (!dock || !dock->isFloating()) {
		QMessageBox::information(
			this, QStringLiteral("OBS Auto Stop"),
			ObsText("Dock.Minimize.NotFloating"));
		return;
	}
	ensureFloatingMinimizeButton();
	dock->showMinimized();
}

void AutoStopDock::updateCombineHint()
{
	if (!combineHintLabel_ || !combineModeCombo_) {
		return;
	}
	const int mode = combineModeCombo_->currentData().toInt();
	if (mode == static_cast<int>(StopCombineMode::And)) {
		combineHintLabel_->setText(ObsText("Dock.Combine.HintAnd"));
	} else {
		combineHintLabel_->setText(ObsText("Dock.Combine.HintOr"));
	}
}

void AutoStopDock::onCombineModeChanged(int)
{
	if (stop_ && combineModeCombo_) {
		stop_->setCombineMode(static_cast<StopCombineMode>(
			combineModeCombo_->currentData().toInt()));
	}
	updateCombineHint();
	updateAndWarning();
	scheduleSave();
}

void AutoStopDock::onOpenRecordingFolder()
{
	QString folder;

	char *path = obs_frontend_get_current_record_output_path();
	if (path) {
		folder = QString::fromUtf8(path);
		bfree(path);
	}

	if (folder.isEmpty()) {
		config_t *cfg = obs_frontend_get_profile_config();
		if (cfg) {
			const char *mode = config_get_string(cfg, "Output", "Mode");
			const bool advanced = mode && strcmp(mode, "Advanced") == 0;
			const char *rec = nullptr;
			if (advanced) {
				const char *type = config_get_string(cfg, "AdvOut", "RecType");
				const bool ffmpeg = type && strcmp(type, "FFmpeg") == 0;
				rec = config_get_string(cfg, "AdvOut",
							ffmpeg ? "FFFilePath" : "RecFilePath");
			} else {
				rec = config_get_string(cfg, "SimpleOutput", "FilePath");
			}
			if (rec && *rec) {
				folder = QString::fromUtf8(rec);
			}
		}
	}

	if (folder.isEmpty()) {
		QMessageBox::information(
			this, QStringLiteral("OBS Auto Stop"),
			ObsText("Dock.Folder.Unknown"));
		return;
	}

	QFileInfo info(folder);
	if (info.isFile()) {
		folder = info.absolutePath();
	}
	if (!QDir(folder).exists()) {
		QMessageBox::warning(
			this, QStringLiteral("OBS Auto Stop"),
			ObsText("Dock.Folder.Missing").arg(folder));
		return;
	}

	QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

