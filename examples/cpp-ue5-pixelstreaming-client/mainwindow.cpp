#include "mainwindow.h"
#include "videowidget.h"
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QDebug>
#include <QCloseEvent>
#include <QTimer>
#include <QThread>


MainWindow::MainWindow(const std::vector<std::string>& streamerList, 
                      DisplayMode mode,
                      QWidget *parent)
    : QMainWindow(parent)
    , m_streamerList(streamerList)
    , m_displayMode(mode)
{
    setupVideoWidgets();
    updateWindowLayout();
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
        widget->setWindowFlags(Qt::Window);
        widget->setWindowTitle(QString::fromStdString(streamId));
        m_videoWidgets[streamId] = widget;
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

void MainWindow::setDisplayMode(DisplayMode mode)
{
    if (m_displayMode != mode) {
        m_displayMode = mode;
        updateWindowLayout();
    }
}

void MainWindow::updateWindowLayout()
{
    if (m_displayMode == FullScreen) {
        distributeWindowsToScreens();
    } else {
        arrangeGridLayout();
    }
}

void MainWindow::arrangeGridLayout()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) return;

    QRect screenGeometry = primaryScreen->geometry();
    int totalWidgets = m_videoWidgets.size();
    
    // 计算网格的行列数
    int cols = qCeil(qSqrt(totalWidgets));
    int rows = qCeil(totalWidgets / (double)cols);
    
    // 计算每个窗口的大小
    int widgetWidth = screenGeometry.width() / cols;
    int widgetHeight = screenGeometry.height() / rows;
    
    // 隐藏所有窗口以准备重新布局
    for (auto& [streamId, widget] : m_videoWidgets) {
        widget->hide();
    }
    
    // 设置网格布局
    int index = 0;
    for (auto& [streamId, widget] : m_videoWidgets) {
        int row = index / cols;
        int col = index % cols;
        
        QRect widgetRect(
            screenGeometry.x() + col * widgetWidth,
            screenGeometry.y() + row * widgetHeight,
            widgetWidth,
            widgetHeight
        );
        
        widget->setGeometry(widgetRect);
        widget->setWindowFlags(Qt::Window);  // 普通窗口模式
        widget->show();
        
        index++;
    }
}