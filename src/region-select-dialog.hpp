#pragma once

#include <QDialog>
#include <QImage>
#include <QMutex>
#include <QPoint>
#include <QRect>
#include <QWidget>

class QLabel;
class QPushButton;
class RegionPreviewCanvas;

struct video_data;
struct video_scale_info;

// Modal dialog: live preview + click-drag region selection (percent of frame).
class RegionSelectDialog : public QDialog {
	Q_OBJECT

public:
	RegionSelectDialog(QWidget *parent, int initX, int initY, int initW,
			   int initH);
	~RegionSelectDialog() override;

	int regionX() const { return region_x_; }
	int regionY() const { return region_y_; }
	int regionW() const { return region_w_; }
	int regionH() const { return region_h_; }

private slots:
	void onOk();
	void onSelectionChanged(const QRect &widgetRect);
	void refreshPreview();

private:
	void registerPreviewCallback();
	void unregisterPreviewCallback();
	void onVideoFrame(const struct video_data *frame);
	static void rawVideoCallback(void *param, struct video_data *frame);
	static struct video_scale_info makeConversion();
	QRect percentToWidgetRect(int x, int y, int w, int h) const;
	void widgetRectToPercent(const QRect &widgetRect, int &x, int &y,
				 int &w, int &h) const;
	QRect videoDisplayRect() const;
	void updateHint();

	RegionPreviewCanvas *canvas_ = nullptr;
	QLabel *hintLabel_ = nullptr;
	QPushButton *okButton_ = nullptr;
	QPushButton *cancelButton_ = nullptr;

	int region_x_ = 0;
	int region_y_ = 0;
	int region_w_ = 100;
	int region_h_ = 100;

	// Pending selection while dialog is open (applied only on OK).
	int pending_x_ = 0;
	int pending_y_ = 0;
	int pending_w_ = 100;
	int pending_h_ = 100;

	bool callback_registered_ = false;
	QMutex frame_mutex_;
	QImage latest_frame_;
	bool frame_dirty_ = false;

	friend class RegionPreviewCanvas;
};

// Canvas that paints the preview frame and rubber-band selection.
class RegionPreviewCanvas : public QWidget {
	Q_OBJECT

public:
	explicit RegionPreviewCanvas(RegionSelectDialog *owner,
				     QWidget *parent = nullptr);

	void setSelectionPercent(int x, int y, int w, int h);
	QRect selectionWidgetRect() const { return selection_; }

signals:
	void selectionChanged(const QRect &widgetRect);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	RegionSelectDialog *owner_ = nullptr;
	QRect selection_;
	bool dragging_ = false;
	QPoint drag_origin_;
};
