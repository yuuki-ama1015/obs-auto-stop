#include "auto-stop-dock.hpp"

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
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QMessageBox>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

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
	combineModeCombo_->addItem(QStringLiteral("または（どれか1つ）"),
				   static_cast<int>(StopCombineMode::Or));
	combineModeCombo_->addItem(QStringLiteral("かつ（すべて）"),
				   static_cast<int>(StopCombineMode::And));
	combineModeCombo_->setToolTip(
		QStringLiteral("静止・無音・メディア終了の組み合わせ方です。
「または」: ONの条件のどれか1つで終了
「かつ」: ONの条件がすべて満たされたら終了
※タイマー（最大録画時間）は常に単独で終了します"));

	openFolderButton_ = new QToolButton(this);
	openFolderButton_->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
	openFolderButton_->setAutoRaise(true);
	openFolderButton_->setToolTip(QStringLiteral("録画の保存先フォルダを開く"));

	combineHintLabel_ = new QLabel(this);
	combineHintLabel_->setWordWrap(true);
	combineHintLabel_->setStyleSheet(QStringLiteral("color: palette(mid);"));

	autoStopCheck_ = new QCheckBox(QStringLiteral("タイマーによって自動で録画終了"), this);
	maxMinutesSpin_ = new QSpinBox(this);
	maxMinutesSpin_->setRange(0, 999);
	maxMinutesSpin_->setSuffix(QStringLiteral(" 分"));
	maxMinutesSpin_->setToolTip(
		QStringLiteral("0 にすると録画タイマーによる自動停止は無効です"));

	auto *timerForm = new QFormLayout;
	timerForm->setContentsMargins(0, 0, 0, 0);
	timerForm->addRow(QStringLiteral("録画タイマー"), maxMinutesSpin_);

	motionCheck_ = new QCheckBox(QStringLiteral("画面が一定時間静止したら自動で録画終了"), this);
	inactivitySpin_ = new QSpinBox(this);
	inactivitySpin_->setRange(1, 600);
	inactivitySpin_->setSuffix(QStringLiteral(" 秒"));
	sensitivitySpin_ = new QDoubleSpinBox(this);
	sensitivitySpin_->setRange(0.1, 50.0);
	sensitivitySpin_->setSingleStep(0.1);
	sensitivitySpin_->setSuffix(QStringLiteral(" %"));
	sensitivitySpin_->setToolTip(QStringLiteral(
		"この起動中だけ有効です。OBSを終了すると既定値（0.5%）に戻ります"));
	minRecordingSpin_ = new QSpinBox(this);
	minRecordingSpin_->setRange(0, 120);
	minRecordingSpin_->setSuffix(QStringLiteral(" 分"));

	auto *motionForm = new QFormLayout;
	motionForm->setContentsMargins(0, 0, 0, 0);
	motionForm->addRow(motionCheck_);
	motionForm->addRow(QStringLiteral("静止と判断する時間"), inactivitySpin_);
	motionForm->addRow(QStringLiteral("静止判定の感度"), sensitivitySpin_);
	motionForm->addRow(QStringLiteral("最低録画時間"), minRecordingSpin_);

	regionCheck_ = new QCheckBox(
		QStringLiteral("└ 監視領域を限定する（静止検出のオプション）"), this);
	regionCheck_->setToolTip(QStringLiteral(
		"「画面が一定時間静止したら自動で録画終了」がONのときだけ有効です"));
	selectRegionButton_ =
		new QPushButton(QStringLiteral("領域を選択"), this);
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
		QStringLiteral("メディアソースの再生が終わったら自動で録画終了"), this);
	mediaEndCheck_->setToolTip(QStringLiteral(
		"ループOFFのメディアソースが終了したら録画を停止します"));

	silenceCheck_ = new QCheckBox(
		QStringLiteral("一定時間無音なら自動で録画終了"), this);
	silenceSpin_ = new QSpinBox(this);
	silenceSpin_->setRange(1, 600);
	silenceSpin_->setSuffix(QStringLiteral(" 秒"));
	silenceThresholdSpin_ = new QDoubleSpinBox(this);
	silenceThresholdSpin_->setRange(-100.0, 0.0);
	silenceThresholdSpin_->setSingleStep(1.0);
	silenceThresholdSpin_->setSuffix(QStringLiteral(" dB"));
	silenceThresholdSpin_->setToolTip(
		QStringLiteral("これ以下のピークレベルを無音とみなします（dBFS）"));

	auto *silenceForm = new QFormLayout;
	silenceForm->setContentsMargins(0, 0, 0, 0);
	silenceForm->addRow(silenceCheck_);
	silenceForm->addRow(QStringLiteral("無音と判断する時間"), silenceSpin_);
	silenceForm->addRow(QStringLiteral("無音しきい値"), silenceThresholdSpin_);

	statusLabel_ = new QLabel(QStringLiteral("プラグインステータス: 待機中"), this);
	elapsedLabel_ = new QLabel(QStringLiteral("経過時間: 0 / — 秒"), this);
	motionLabel_ = new QLabel(QStringLiteral("静止時間: 0 / — 秒"), this);
	mediaLabel_ = new QLabel(QStringLiteral("メディア終了: —"), this);
	silenceLabel_ = new QLabel(QStringLiteral("無音時間: —"), this);

	// Order: combine mode → hint → media end → timer → motion → silence → status → minimize
	auto *combineRow = new QHBoxLayout;
	combineRow->setContentsMargins(0, 0, 0, 0);
	combineRow->addWidget(new QLabel(QStringLiteral("条件の組み合わせ"), this));
	combineRow->addWidget(combineModeCombo_, 1);
	combineRow->addWidget(openFolderButton_);
	layout->addLayout(combineRow);
	layout->addWidget(combineHintLabel_);
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

	minimizeButton_ = new QPushButton(QStringLiteral("タスクバーに最小化"), this);
	minimizeButton_->setToolTip(QStringLiteral(
		"ドックをフロート表示しているとき、タスクバーへ最小化します"));
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

	loadSettings();
	updateCombineHint();
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
	updateRegionControlsEnabled();
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
	saveSettings();
}

void AutoStopDock::onMediaEndToggled(bool enabled)
{
	if (media_) {
		media_->setEnabled(enabled);
	}
	saveSettings();
}

void AutoStopDock::onSilenceToggled(bool enabled)
{
	if (silence_) {
		silence_->setEnabled(enabled);
	}
	saveSettings();
}

void AutoStopDock::onSilenceSecondsChanged(int seconds)
{
	if (silence_) {
		silence_->setSilenceDuration(std::chrono::seconds{seconds});
	}
	saveSettings();
}

void AutoStopDock::onSilenceThresholdChanged(double db)
{
	if (silence_) {
		silence_->setThresholdDb(db);
	}
	saveSettings();
}

void AutoStopDock::onRegionToggled(bool enabled)
{
	if (motion_) {
		motion_->setRegionEnabled(enabled);
	}
	updateRegionControlsEnabled();
	saveSettings();
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
	saveSettings();
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
		QStringLiteral("領域: %1%, %2%  %3%×%4%")
			.arg(regionX_)
			.arg(regionY_)
			.arg(regionW_)
			.arg(regionH_));
}

void AutoStopDock::refreshStatus()
{
	if (!monitor_) {
		return;
	}

	const auto elapsed = monitor_->elapsedRecordingTime().count();
	const auto maxSec = monitor_->maxRecordingDuration().count();

	if (!monitor_->isEnabled()) {
		statusLabel_->setText(
			QStringLiteral("プラグインステータス: 自動停止 OFF"));
	} else if (monitor_->isRecording()) {
		statusLabel_->setText(QStringLiteral("プラグインステータス: 録画中"));
	} else if (stop_ && stop_->stopRequested()) {
		statusLabel_->setText(
			QStringLiteral("プラグインステータス: 自動停止要求中"));
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
		QString regionNote;
		if (motion_->isRegionEnabled()) {
			int x, y, w, h;
			motion_->regionPercent(x, y, w, h);
			regionNote = QStringLiteral(" / 領域 %1%,%2% %3%x%4%")
					     .arg(x)
					     .arg(y)
					     .arg(w)
					     .arg(h);
		}
		motionLabel_->setText(
			QStringLiteral("静止時間: %1 / %2 秒 (動き %3%)%4")
				.arg(static_cast<qlonglong>(still))
				.arg(static_cast<qlonglong>(need))
				.arg(motion_->lastMotionPercent(), 0, 'f', 2)
				.arg(regionNote));
	} else {
		motionLabel_->setText(QStringLiteral("静止時間: —"));
	}

	if (media_ && media_->isEnabled()) {
		const auto name = media_->endedSourceName();
		if (!name.empty()) {
			mediaLabel_->setText(
				QStringLiteral("メディア終了: 検知 (%1)")
					.arg(QString::fromStdString(name)));
		} else {
			mediaLabel_->setText(QStringLiteral("メディア終了: 監視中"));
		}
	} else {
		mediaLabel_->setText(QStringLiteral("メディア終了: —"));
	}

	if (silence_ && silence_->isEnabled()) {
		const auto still = silence_->silenceActiveDuration().count();
		const auto need = silence_->silenceDuration().count();
		silenceLabel_->setText(
			QStringLiteral("無音時間: %1 / %2 秒 (レベル %3 dB)")
				.arg(static_cast<qlonglong>(still))
				.arg(static_cast<qlonglong>(need))
				.arg(silence_->lastLevelDb(), 0, 'f', 1));
	} else {
		silenceLabel_->setText(QStringLiteral("無音時間: —"));
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
		regionCheck_->setChecked(false);
		regionX_ = 0;
		regionY_ = 0;
		regionW_ = 100;
		regionH_ = 100;
		mediaEndCheck_->setChecked(false);
		silenceCheck_->setChecked(false);
		silenceSpin_->setValue(kDefaultSilenceSec);
		silenceThresholdSpin_->setValue(kDefaultSilenceThresholdDb);
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
			motion_->setRegionEnabled(false);
			motion_->setRegionPercent(0, 0, 100, 100);
		}
		if (media_) {
			media_->setEnabled(false);
			media_->setMinimumRecordingDuration(
				std::chrono::seconds{kDefaultMinRecordingMin *
						     60});
		}
		if (silence_) {
			silence_->setEnabled(false);
			silence_->setSilenceDuration(
				std::chrono::seconds{kDefaultSilenceSec});
			silence_->setThresholdDb(kDefaultSilenceThresholdDb);
			silence_->setMinimumRecordingDuration(
				std::chrono::seconds{kDefaultMinRecordingMin *
						     60});
		}
	};

	if (!config) {
		applyDefaults();
		updateRegionStatusLabel();
		updateRegionControlsEnabled();
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
	config_set_default_bool(config, kConfigSection, kKeyRegionEnabled,
				false);
	config_set_default_int(config, kConfigSection, kKeyRegionX, 0);
	config_set_default_int(config, kConfigSection, kKeyRegionY, 0);
	config_set_default_int(config, kConfigSection, kKeyRegionW, 100);
	config_set_default_int(config, kConfigSection, kKeyRegionH, 100);
	config_set_default_bool(config, kConfigSection, kKeyMediaEndEnabled,
				false);
	config_set_default_int(config, kConfigSection, kKeyCombineMode, 0);
	config_set_default_bool(config, kConfigSection, kKeySilenceEnabled,
				false);
	config_set_default_int(config, kConfigSection, kKeySilenceSec,
			       kDefaultSilenceSec);
	config_set_default_double(config, kConfigSection,
				  kKeySilenceThresholdDb,
				  kDefaultSilenceThresholdDb);

	const bool enabled =
		config_get_bool(config, kConfigSection, kKeyEnabled);
	int minutes = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyMaxMinutes));
	const bool motionEnabled =
		config_get_bool(config, kConfigSection, kKeyMotionEnabled);
	int inactivity = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyInactivitySec));
	// Stillness sensitivity always starts at the default each launch.
	const double sensitivity = kDefaultSensitivity;
	int minRecMin = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyMinRecordingMin));
	const bool regionEnabled =
		config_get_bool(config, kConfigSection, kKeyRegionEnabled);
	int rx = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyRegionX));
	int ry = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyRegionY));
	int rw = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyRegionW));
	int rh = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyRegionH));
	const bool mediaEndEnabled =
		config_get_bool(config, kConfigSection, kKeyMediaEndEnabled);
	int combineMode = static_cast<int>(
		config_get_int(config, kConfigSection, kKeyCombineMode));
	if (combineMode != 0 && combineMode != 1) {
		combineMode = 0;
	}
	const bool silenceEnabled =
		config_get_bool(config, kConfigSection, kKeySilenceEnabled);
	int silenceSec = static_cast<int>(
		config_get_int(config, kConfigSection, kKeySilenceSec));
	const double silenceDb = config_get_double(
		config, kConfigSection, kKeySilenceThresholdDb);

	if (minutes < 0)
		minutes = 0;
	if (minutes > 999)
		minutes = 999;
	if (inactivity < 1)
		inactivity = 1;
	if (minRecMin < 0)
		minRecMin = 0;
	if (silenceSec < 1)
		silenceSec = 1;

	const bool old1 = autoStopCheck_->blockSignals(true);
	const bool old2 = maxMinutesSpin_->blockSignals(true);
	const bool old3 = motionCheck_->blockSignals(true);
	const bool old4 = inactivitySpin_->blockSignals(true);
	const bool old5 = sensitivitySpin_->blockSignals(true);
	const bool old6 = minRecordingSpin_->blockSignals(true);
	const bool old7 = regionCheck_->blockSignals(true);
	const bool old8 = mediaEndCheck_->blockSignals(true);
	const bool oldCombine = combineModeCombo_->blockSignals(true);
	const bool old9 = silenceCheck_->blockSignals(true);
	const bool old10 = silenceSpin_->blockSignals(true);
	const bool old11 = silenceThresholdSpin_->blockSignals(true);

	autoStopCheck_->setChecked(enabled);
	maxMinutesSpin_->setValue(minutes);
	motionCheck_->setChecked(motionEnabled);
	inactivitySpin_->setValue(inactivity);
	sensitivitySpin_->setValue(sensitivity);
	minRecordingSpin_->setValue(minRecMin);
	regionCheck_->setChecked(regionEnabled);
	regionX_ = rx;
	regionY_ = ry;
	regionW_ = rw;
	regionH_ = rh;
	mediaEndCheck_->setChecked(mediaEndEnabled);
	const int combineIdx = combineModeCombo_->findData(combineMode);
	combineModeCombo_->setCurrentIndex(combineIdx >= 0 ? combineIdx : 0);
	if (stop_) {
		stop_->setCombineMode(static_cast<StopCombineMode>(combineMode));
	}
	silenceCheck_->setChecked(silenceEnabled);
	silenceSpin_->setValue(silenceSec);
	silenceThresholdSpin_->setValue(silenceDb);

	autoStopCheck_->blockSignals(old1);
	maxMinutesSpin_->blockSignals(old2);
	motionCheck_->blockSignals(old3);
	inactivitySpin_->blockSignals(old4);
	sensitivitySpin_->blockSignals(old5);
	minRecordingSpin_->blockSignals(old6);
	regionCheck_->blockSignals(old7);
	mediaEndCheck_->blockSignals(old8);
	combineModeCombo_->blockSignals(oldCombine);
	silenceCheck_->blockSignals(old9);
	silenceSpin_->blockSignals(old10);
	silenceThresholdSpin_->blockSignals(old11);

	if (monitor_) {
		monitor_->setEnabled(enabled);
		monitor_->setMaxRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minutes) * 60});
	}
	if (motion_) {
		motion_->setEnabled(motionEnabled);
		motion_->setInactivityDuration(std::chrono::seconds{inactivity});
		motion_->setSensitivityPercent(sensitivity);
		motion_->setMinimumRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minRecMin) *
					     60});
		motion_->setRegionEnabled(regionEnabled);
		motion_->setRegionPercent(rx, ry, rw, rh);
		motion_->regionPercent(regionX_, regionY_, regionW_, regionH_);
	}
	if (media_) {
		media_->setEnabled(mediaEndEnabled);
		media_->setMinimumRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minRecMin) *
					     60});
	}
	if (silence_) {
		silence_->setEnabled(silenceEnabled);
		silence_->setSilenceDuration(std::chrono::seconds{silenceSec});
		silence_->setThresholdDb(silenceDb);
		silence_->setMinimumRecordingDuration(
			std::chrono::seconds{static_cast<int64_t>(minRecMin) *
					     60});
	}

	updateRegionStatusLabel();
	updateRegionControlsEnabled();
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
	// Do not persist SensitivityPercent — reset to default next launch.
	config_set_double(config, kConfigSection, kKeySensitivity,
			  kDefaultSensitivity);
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

QWidget *AutoStopDock::floatingWindow() const
{
	for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
		if (w->isWindow() && w != this) {
			return w;
		}
	}
	return nullptr;
}

void AutoStopDock::ensureFloatingMinimizeButton()
{
	QWidget *win = floatingWindow();
	if (!win) {
		return;
	}
	const Qt::WindowFlags flags = win->windowFlags();
	if (flags & Qt::WindowMinimizeButtonHint) {
		return;
	}
	win->setWindowFlags(flags | Qt::Window | Qt::WindowMinimizeButtonHint |
			    Qt::WindowCloseButtonHint);
	win->show();
}

void AutoStopDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	ensureFloatingMinimizeButton();
}

void AutoStopDock::onMinimizeToTaskbar()
{
	ensureFloatingMinimizeButton();
	QWidget *win = floatingWindow();
	if (!win || !win->isWindow()) {
		QMessageBox::information(
			this, QStringLiteral("OBS Auto Stop"),
			QStringLiteral(
				"フロート表示のときだけタスクバーに最小化できます。\n"
				"ドックをウィンドウから切り離してから、もう一度押してください。"));
		return;
	}
	win->showMinimized();
}

void AutoStopDock::updateCombineHint()
{
	if (!combineHintLabel_ || !combineModeCombo_) {
		return;
	}
	const int mode = combineModeCombo_->currentData().toInt();
	if (mode == static_cast<int>(StopCombineMode::And)) {
		combineHintLabel_->setText(QStringLiteral(
			"ONにした静止・無音・メディア終了は「かつ」で判定します。すべて満たしたら録画を終了します（タイマーは単独で終了）。"));
	} else {
		combineHintLabel_->setText(QStringLiteral(
			"ONにした静止・無音・メディア終了は「または」で判定します。どれか1つでも満たしたら録画を終了します（タイマーは単独で終了）。"));
	}
}

void AutoStopDock::onCombineModeChanged(int)
{
	if (stop_ && combineModeCombo_) {
		stop_->setCombineMode(static_cast<StopCombineMode>(
			combineModeCombo_->currentData().toInt()));
	}
	updateCombineHint();
	saveSettings();
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
				rec = config_get_string(cfg, "AdvOut", "RecFilePath");
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
			QStringLiteral("録画の保存先を取得できませんでした。"));
		return;
	}

	QFileInfo info(folder);
	if (info.isFile()) {
		folder = info.absolutePath();
	}
	if (!QDir(folder).exists()) {
		QMessageBox::warning(
			this, QStringLiteral("OBS Auto Stop"),
			QStringLiteral("保存先フォルダが存在しません:\n%1").arg(folder));
		return;
	}

	QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

