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
    int screenCount = screens.size();

    if (screenCount == 0) {
        qDebug() << "Error: No screens detected!";
        return; // Exit if no screens are available to avoid crashes
    }

    // Dynamically generate a list of available screens
    std::vector<QScreen*> availableScreens;
    for (QScreen* screen : screens) {
        availableScreens.push_back(screen);
    }

    // Hide all video widgets initially to prepare for repositioning
    for (auto& [streamId, widget] : m_videoWidgets) {
        widget->hide();
    }

    // Distribute video widgets across screens
    int screenIndex = 0;
    for (auto& [streamId, widget] : m_videoWidgets) {
        QScreen* targetScreen = availableScreens[screenIndex];

        // Get the geometry of the target screen
        QRect geometry = targetScreen->geometry();

        // Configure the widget to fit the target screen
        widget->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        widget->setScreen(targetScreen);
        widget->setGeometry(geometry);

        // Use a timer to ensure the widget displays correctly
        QTimer::singleShot(500 * screenIndex, this, [widget, geometry]() {
            widget->showFullScreen(); // Show the widget in full-screen mode
            widget->setGeometry(geometry); // Ensure it matches the screen geometry
        });

        // Cycle through screens if the number of widgets exceeds available screens
        screenIndex = (screenIndex + 1) % screenCount;
    }

    qDebug() << "Distributed windows to" << screenCount << "screens.";
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
