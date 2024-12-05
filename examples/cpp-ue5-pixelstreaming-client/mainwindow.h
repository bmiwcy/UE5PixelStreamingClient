#pragma once

#include <QMainWindow>
#include <QVector>
#include <QScreen>
#include <opencv2/core.hpp>
#include <memory>
#include <unordered_map>
#include <queue>
#include <mutex>

class VideoWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum DisplayMode {
        FullScreen,
        GridLayout
    };

    explicit MainWindow(const std::vector<std::string>& streamerList, 
                       DisplayMode mode = GridLayout,  // 默认使用网格布局
                       QWidget *parent = nullptr);
    ~MainWindow();

    void addFrame(const std::string& streamId, const cv::Mat& frame);
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_displayMode; }

private slots:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupVideoWidgets();
    void distributeWindowsToScreens();  // 全屏模式的布局
    void arrangeGridLayout();          // 网格模式的布局
    void updateWindowLayout();         // 更新窗口布局

private:
    std::vector<std::string> m_streamerList;
    std::unordered_map<std::string, VideoWidget*> m_videoWidgets;
    std::mutex m_mutex;
    DisplayMode m_displayMode;
};
