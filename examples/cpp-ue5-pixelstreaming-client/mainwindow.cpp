#include "mainwindow.h"
#include "videowidget.h"
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QDebug>
#include <QCloseEvent>
#include <QTimer>
#include <QThread>

MainWindow::MainWindow(const std::vector<std::string>& streamerList, QWidget *parent)
    : QMainWindow(parent)
    , m_streamerList(streamerList)
{
    setupVideoWidgets();
    distributeWindowsToScreens();
}

MainWindow::~MainWindow()
{
    for (auto& [streamId, widget] : m_videoWidgets) {
        delete widget;
    }
}

void MainWindow::setupVideoWidgets()
{
    for (const auto& streamId : m_streamerList) {
        auto* widget = new VideoWidget(streamId);
        widget->setWindowFlags(Qt::Window);  // Make it a standalone window
        widget->setWindowTitle(QString::fromStdString(streamId));
        m_videoWidgets[streamId] = widget;
        widget->show();
    }
}

void MainWindow::distributeWindowsToScreens()
{
    QList<QScreen*> screens = QGuiApplication::screens();
    
    // Hide all windows initially
    for (auto& [streamId, widget] : m_videoWidgets) {
        widget->hide();
    }

    // Mapping relationship
    struct ScreenInfo {
        QScreen* screen;
        QRect geometry;
        QString position;
    };

    std::vector<ScreenInfo> screenInfos = {
        {screens[0], screens[0]->geometry(), "Center"}, // XWAYLAND0
        {screens[1], screens[1]->geometry(), "Left"},   // XWAYLAND1
        {screens[2], screens[2]->geometry(), "Right"}   // XWAYLAND2
    };

    std::map<std::string, int> streamToScreenIndex = {
        {"Camera01_Default", 1}, // Left
        {"Camera02_Default", 0}, // Center
        {"Camera03_Default", 2}  // Right
    };

    for (auto& [streamId, widget] : m_videoWidgets) {
        int screenIndex = streamToScreenIndex[streamId];
        if (screenIndex < screenInfos.size()) {
            const auto& info = screenInfos[screenIndex];
            QString qStreamId = QString::fromStdString(streamId);
            
            // Set window properties
            widget->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
            widget->setScreen(info.screen);
            
            // Ensure the window position fully matches the screen
            widget->setGeometry(info.geometry);
            
            QTimer::singleShot(500 * screenIndex, this, [widget, info, qStreamId]() {
                if (widget) {
                    
                    // Re-confirm screen and position
                    widget->setScreen(info.screen);
                    widget->setGeometry(info.geometry);
                    widget->showFullScreen();
                    
                    QTimer::singleShot(100, widget, [widget, info, qStreamId]() {
                        
                        // Force the correct geometry
                        widget->setGeometry(info.geometry);
                        
                        // Final check
                        QTimer::singleShot(50, widget, [widget, info, qStreamId]() {
                            if (widget->geometry() != info.geometry) {
                                widget->setGeometry(info.geometry);
                            }
                        });
                    });
                }
            });
        }
    }
}

void MainWindow::addFrame(const std::string& streamId, const cv::Mat& frame)
{
    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_videoWidgets.find(streamId);
        if (it != m_videoWidgets.end() && it->second) {
            QMetaObject::invokeMethod(it->second, [widget = it->second, frame]() {
                widget->updateFrame(frame);
            }, Qt::QueuedConnection);
        } else {
            qDebug() << "Error: No VideoWidget found for streamId:" << QString::fromStdString(streamId);
        }
    } catch (const std::exception& e) {
        qDebug() << "Exception in MainWindow::addFrame:" << e.what();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (auto& [streamId, widget] : m_videoWidgets) {
        widget->close();
    }
    event->accept();
}
