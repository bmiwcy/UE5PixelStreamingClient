#pragma once

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <QKeyEvent>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

class VideoWidget : public QWidget {
    Q_OBJECT

public:
    enum DisplayMode {
        FullScreen,
        GridLayout
    };

    explicit VideoWidget(const std::string& streamId, QWidget *parent = nullptr);
    void updateFrame(const cv::Mat& frame);
    void showFullScreen();
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_displayMode; }
    void setGridPosition(const QRect& rect);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    QImage m_image;
    std::string m_streamId;
    Qt::AspectRatioMode m_aspectRatioMode;
    bool m_keepAspectRatio;
    DisplayMode m_displayMode;
    QRect m_gridRect;
};