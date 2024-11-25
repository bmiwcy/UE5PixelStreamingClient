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
    explicit MainWindow(const std::vector<std::string>& streamerList, QWidget *parent = nullptr);
    ~MainWindow();

    void addFrame(const std::string& streamId, const cv::Mat& frame);

private slots:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupVideoWidgets();
    void distributeWindowsToScreens();

private:
    std::vector<std::string> m_streamerList;
    std::unordered_map<std::string, VideoWidget*> m_videoWidgets;
    std::mutex m_mutex;
};