#include "region-select-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <media-io/video-io.h>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr uint32_t kPreviewWidth = 640;
constexpr uint32_t kPreviewHeight = 360;
constexpr uint32_t kPreviewFrameDivisor = 3; // ~10 fps preview
constexpr int kMinPercent = 1;

int clampInt(int value, int lo, int hi)
{
	return std::max(lo, std::min(hi, value));
}

void clampRegionPercent(int &x, int &y, int &w, int &h)
{
	x = clampInt(x, 0, 100);
	y = clampInt(y, 0, 100);
	w = clampInt(w, kMinPercent, 100);
	h = clampInt(h, kMinPercent, 100);
	if (x + w > 100) {
		x = 100 - w;
	}
	if (y + h > 100) {
		y = 100 - h;
	}
	if (x < 0) {
		x = 0;
	}
	if (y < 0) {
		y = 0;
	}
}
} // namespace

RegionPreviewCanvas::RegionPreviewCanvas(RegionSelectDialog *owner,
					 QWidget *parent)
	: QWidget(parent), owner_(owner)
{
	setMinimumSize(480, 270);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setMouseTracking(true);
	setCursor(Qt::CrossCursor);
	setFocusPolicy(Qt::StrongFocus);
}

void RegionPreviewCanvas::setSelectionPercent(int x, int y, int w, int h)
{
	if (!owner_) {
		return;
	}
	selection_ = owner_->percentToWidgetRect(x, y, w, h);
	update();
}

void RegionPreviewCanvas::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(24, 24, 24));

	QImage frame;
	if (owner_) {
		QMutexLocker lock(&owner_->frame_mutex_);
		frame = owner_->latest_frame_;
	}

	const QRect videoRect = owner_ ? owner_->videoDisplayRect() : rect();
	if (!frame.isNull() && videoRect.isValid()) {
		p.drawImage(videoRect, frame);
	} else {
		p.setPen(QColor(180, 180, 180));
		p.drawText(rect(), Qt::AlignCenter,
			   QStringLiteral("プレビュー待機中…\n（OBS の映像出力が必要です）"));
	}

	// Dim outside selection
	if (selection_.isValid() && selection_.width() > 0 &&
	    selection_.height() > 0) {
		QRect sel = selection_.intersected(videoRect.isValid()
							   ? videoRect
							   : rect());
		if (sel.isValid()) {
			QColor dim(0, 0, 0, 120);
			// top
			p.fillRect(QRect(videoRect.left(), videoRect.top(),
					 videoRect.width(),
					 sel.top() - videoRect.top()),
				   dim);
			// bottom
			p.fillRect(QRect(videoRect.left(), sel.bottom() + 1,
					 videoRect.width(),
					 videoRect.bottom() - sel.bottom()),
				   dim);
			// left
			p.fillRect(QRect(videoRect.left(), sel.top(),
					 sel.left() - videoRect.left(),
					 sel.height()),
				   dim);
			// right
			p.fillRect(QRect(sel.right() + 1, sel.top(),
					 videoRect.right() - sel.right(),
					 sel.height()),
				   dim);

			p.setPen(QPen(QColor(0, 200, 255), 2));
			p.setBrush(Qt::NoBrush);
			p.drawRect(sel.adjusted(0, 0, -1, -1));
		}
	}
}

void RegionPreviewCanvas::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !owner_) {
		return;
	}
	const QRect videoRect = owner_->videoDisplayRect();
	QPoint pos = event->pos();
	if (videoRect.isValid()) {
		pos.setX(clampInt(pos.x(), videoRect.left(), videoRect.right()));
		pos.setY(clampInt(pos.y(), videoRect.top(), videoRect.bottom()));
	}
	dragging_ = true;
	drag_origin_ = pos;
	selection_ = QRect(pos, QSize(1, 1));
	update();
	emit selectionChanged(selection_);
}

void RegionPreviewCanvas::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging_ || !owner_) {
		return;
	}
	const QRect videoRect = owner_->videoDisplayRect();
	QPoint pos = event->pos();
	if (videoRect.isValid()) {
		pos.setX(clampInt(pos.x(), videoRect.left(), videoRect.right()));
		pos.setY(clampInt(pos.y(), videoRect.top(), videoRect.bottom()));
	}
	selection_ = QRect(drag_origin_, pos).normalized();
	if (selection_.width() < 1) {
		selection_.setWidth(1);
	}
	if (selection_.height() < 1) {
		selection_.setHeight(1);
	}
	update();
	emit selectionChanged(selection_);
}

void RegionPreviewCanvas::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !dragging_) {
		return;
	}
	dragging_ = false;
	mouseMoveEvent(event);
}

void RegionPreviewCanvas::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (owner_) {
		setSelectionPercent(owner_->pending_x_, owner_->pending_y_,
				    owner_->pending_w_, owner_->pending_h_);
	}
}

RegionSelectDialog::RegionSelectDialog(QWidget *parent, int initX, int initY,
				       int initW, int initH)
	: QDialog(parent)
{
	setWindowTitle(QStringLiteral("監視領域を選択"));
	setModal(true);
	resize(720, 520);

	region_x_ = initX;
	region_y_ = initY;
	region_w_ = initW;
	region_h_ = initH;
	clampRegionPercent(region_x_, region_y_, region_w_, region_h_);
	pending_x_ = region_x_;
	pending_y_ = region_y_;
	pending_w_ = region_w_;
	pending_h_ = region_h_;

	canvas_ = new RegionPreviewCanvas(this, this);
	hintLabel_ = new QLabel(this);
	hintLabel_->setWordWrap(true);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	okButton_ = buttons->button(QDialogButtonBox::Ok);
	cancelButton_ = buttons->button(QDialogButtonBox::Cancel);
	okButton_->setText(QStringLiteral("OK"));
	cancelButton_->setText(QStringLiteral("キャンセル"));

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(canvas_, 1);
	layout->addWidget(hintLabel_);
	layout->addWidget(buttons);

	connect(canvas_, &RegionPreviewCanvas::selectionChanged, this,
		&RegionSelectDialog::onSelectionChanged);
	connect(okButton_, &QPushButton::clicked, this,
		&RegionSelectDialog::onOk);
	connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);

	canvas_->setSelectionPercent(pending_x_, pending_y_, pending_w_,
				     pending_h_);
	updateHint();

	auto *timer = new QTimer(this);
	timer->setInterval(50);
	connect(timer, &QTimer::timeout, this,
		&RegionSelectDialog::refreshPreview);
	timer->start();

	registerPreviewCallback();
}

RegionSelectDialog::~RegionSelectDialog()
{
	unregisterPreviewCallback();
}

void RegionSelectDialog::onOk()
{
	region_x_ = pending_x_;
	region_y_ = pending_y_;
	region_w_ = pending_w_;
	region_h_ = pending_h_;
	clampRegionPercent(region_x_, region_y_, region_w_, region_h_);
	accept();
}

void RegionSelectDialog::onSelectionChanged(const QRect &widgetRect)
{
	widgetRectToPercent(widgetRect, pending_x_, pending_y_, pending_w_,
			    pending_h_);
	updateHint();
}

void RegionSelectDialog::refreshPreview()
{
	bool dirty = false;
	{
		QMutexLocker lock(&frame_mutex_);
		dirty = frame_dirty_;
		frame_dirty_ = false;
	}
	if (dirty && canvas_) {
		canvas_->update();
	}
}

void RegionSelectDialog::updateHint()
{
	hintLabel_->setText(
		QStringLiteral(
			"ドラッグで領域を選択してください。現在: %1%, %2%  %3%×%4%")
			.arg(pending_x_)
			.arg(pending_y_)
			.arg(pending_w_)
			.arg(pending_h_));
}

struct video_scale_info RegionSelectDialog::makeConversion()
{
	struct video_scale_info info {};
	info.format = VIDEO_FORMAT_Y800;
	info.width = kPreviewWidth;
	info.height = kPreviewHeight;
	info.range = VIDEO_RANGE_FULL;
	info.colorspace = VIDEO_CS_DEFAULT;
	return info;
}

void RegionSelectDialog::rawVideoCallback(void *param,
					  struct video_data *frame)
{
	static_cast<RegionSelectDialog *>(param)->onVideoFrame(frame);
}

void RegionSelectDialog::onVideoFrame(const struct video_data *frame)
{
	if (!frame || !frame->data[0]) {
		return;
	}

	const uint32_t w = kPreviewWidth;
	const uint32_t h = kPreviewHeight;
	const uint32_t stride = frame->linesize[0];

	QImage img(static_cast<int>(w), static_cast<int>(h),
		   QImage::Format_Grayscale8);
	for (uint32_t y = 0; y < h; ++y) {
		const uint8_t *src =
			frame->data[0] + static_cast<size_t>(y) * stride;
		std::memcpy(img.scanLine(static_cast<int>(y)), src, w);
	}

	QMutexLocker lock(&frame_mutex_);
	latest_frame_ = img;
	frame_dirty_ = true;
}

void RegionSelectDialog::registerPreviewCallback()
{
	if (callback_registered_) {
		return;
	}
	struct video_scale_info conversion = makeConversion();
	obs_add_raw_video_callback2(&conversion, kPreviewFrameDivisor,
				    rawVideoCallback, this);
	callback_registered_ = true;
}

void RegionSelectDialog::unregisterPreviewCallback()
{
	if (!callback_registered_) {
		return;
	}
	obs_remove_raw_video_callback(rawVideoCallback, this);
	callback_registered_ = false;
}

QRect RegionSelectDialog::videoDisplayRect() const
{
	if (!canvas_) {
		return QRect();
	}

	const QRect bounds = canvas_->rect();
	if (bounds.width() < 2 || bounds.height() < 2) {
		return bounds;
	}

	int baseW = 0;
	int baseH = 0;
	struct obs_video_info ovi {};
	if (obs_get_video_info(&ovi) && ovi.base_width > 0 &&
	    ovi.base_height > 0) {
		baseW = static_cast<int>(ovi.base_width);
		baseH = static_cast<int>(ovi.base_height);
	} else {
		baseW = static_cast<int>(kPreviewWidth);
		baseH = static_cast<int>(kPreviewHeight);
	}

	const double scale = std::min(static_cast<double>(bounds.width()) /
					      static_cast<double>(baseW),
				      static_cast<double>(bounds.height()) /
					      static_cast<double>(baseH));
	const int drawW = std::max(1, static_cast<int>(std::round(baseW * scale)));
	const int drawH = std::max(1, static_cast<int>(std::round(baseH * scale)));
	const int x = bounds.left() + (bounds.width() - drawW) / 2;
	const int y = bounds.top() + (bounds.height() - drawH) / 2;
	return QRect(x, y, drawW, drawH);
}

QRect RegionSelectDialog::percentToWidgetRect(int x, int y, int w, int h) const
{
	const QRect video = videoDisplayRect();
	if (!video.isValid()) {
		return QRect();
	}
	const int left =
		video.left() +
		static_cast<int>(std::lround(video.width() * (x / 100.0)));
	const int top =
		video.top() +
		static_cast<int>(std::lround(video.height() * (y / 100.0)));
	const int right =
		video.left() +
		static_cast<int>(
			std::lround(video.width() * ((x + w) / 100.0))) -
		1;
	const int bottom =
		video.top() +
		static_cast<int>(
			std::lround(video.height() * ((y + h) / 100.0))) -
		1;
	return QRect(QPoint(left, top), QPoint(right, bottom)).normalized();
}

void RegionSelectDialog::widgetRectToPercent(const QRect &widgetRect, int &x,
					     int &y, int &w, int &h) const
{
	const QRect video = videoDisplayRect();
	if (!video.isValid() || video.width() < 1 || video.height() < 1) {
		// Fallback: use canvas size as 100%.
		const QRect bounds = canvas_ ? canvas_->rect() : widgetRect;
		const double bw = std::max(1, bounds.width());
		const double bh = std::max(1, bounds.height());
		x = clampInt(static_cast<int>(std::lround(
				     100.0 * (widgetRect.left() - bounds.left()) /
				     bw)),
			     0, 99);
		y = clampInt(static_cast<int>(std::lround(
				     100.0 * (widgetRect.top() - bounds.top()) /
				     bh)),
			     0, 99);
		w = clampInt(static_cast<int>(std::lround(
				     100.0 * widgetRect.width() / bw)),
			     kMinPercent, 100);
		h = clampInt(static_cast<int>(std::lround(
				     100.0 * widgetRect.height() / bh)),
			     kMinPercent, 100);
		clampRegionPercent(x, y, w, h);
		return;
	}

	const QRect sel = widgetRect.intersected(video);
	const double vw = static_cast<double>(video.width());
	const double vh = static_cast<double>(video.height());

	x = clampInt(static_cast<int>(std::lround(
			     100.0 * (sel.left() - video.left()) / vw)),
		     0, 99);
	y = clampInt(static_cast<int>(std::lround(
			     100.0 * (sel.top() - video.top()) / vh)),
		     0, 99);
	const int x2 = clampInt(
		static_cast<int>(std::lround(
			100.0 * (sel.right() + 1 - video.left()) / vw)),
		1, 100);
	const int y2 = clampInt(
		static_cast<int>(std::lround(
			100.0 * (sel.bottom() + 1 - video.top()) / vh)),
		1, 100);
	w = std::max(kMinPercent, x2 - x);
	h = std::max(kMinPercent, y2 - y);
	clampRegionPercent(x, y, w, h);
}
