#include "videowidget.h"
#include <QResizeEvent>
#include <QPainter>
#include <QDebug>

VideoWidget::VideoWidget(const std::string& streamId, QWidget *parent)
    : QWidget(parent)
    , m_streamId(streamId)
    , m_aspectRatioMode(Qt::KeepAspectRatio)
    , m_keepAspectRatio(true)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);  // Allow receiving keyboard events
}

void VideoWidget::updateFrame(const cv::Mat& frame)
{
    if (frame.empty()) {
        return;
    }

    try {
        cv::Mat temp;
        if (frame.channels() == 3) {
            cv::cvtColor(frame.clone(), temp, cv::COLOR_BGR2RGB);
        } else {
            temp = frame.clone();
        }

        m_image = QImage((const unsigned char*)(temp.data),
                        temp.cols, temp.rows,
                        temp.step,
                        QImage::Format_RGB888).copy(); // Create a deep copy

        update();
    } catch (const std::exception& e) {
    }
}

void VideoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    
    // If no image is available, draw a red background
    if (m_image.isNull()) {
        painter.fillRect(rect(), QColor(255, 0, 0));  // Red background
        return;
    }

    // Draw the image if available
    if (m_keepAspectRatio) {
        // Calculate the display area that maintains the aspect ratio
        QRect targetRect = rect();
        QSize scaled = m_image.size().scaled(targetRect.size(), m_aspectRatioMode);
        targetRect.setSize(scaled);
        targetRect.moveCenter(rect().center());

        // Fill the background with black
        painter.fillRect(rect(), Qt::black);
        // Draw the image
        painter.drawImage(targetRect, m_image);
    } else {
        // Stretch the image to fill the entire window
        painter.drawImage(rect(), m_image);
    }
}

void VideoWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
}

void VideoWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
}

void VideoWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        // Toggle display mode on space key press
        m_keepAspectRatio = !m_keepAspectRatio;
        update();
    } else if (event->key() == Qt::Key_Escape) {
        // Exit full screen mode on Escape key press
        if (isFullScreen()) {
            showNormal();
        }
    }
    QWidget::keyPressEvent(event);
}

bool VideoWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    // Handle native window events here
    if (isFullScreen()) {
        QScreen* screen = this->screen();
        if (screen && geometry() != screen->geometry()) {
            setGeometry(screen->geometry());
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void VideoWidget::showFullScreen()
{
    QScreen* screen = this->screen();
    if (screen) {
        QRect screenGeometry = screen->geometry();
        
        setGeometry(screenGeometry);
        QWidget::showFullScreen();
    }
}

void VideoWidget::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
             
    if (isFullScreen()) {
        QScreen* screen = this->screen();
        if (screen && geometry() != screen->geometry()) {
            setGeometry(screen->geometry());
        }
    }
}

void VideoWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}
