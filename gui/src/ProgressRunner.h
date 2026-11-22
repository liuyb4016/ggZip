#pragma once
// 后台线程执行引擎操作，通过队列信号把进度/结果送回 GUI 线程。
#include <QByteArray>
#include <QString>
#include <atomic>
#include <functional>
#include <thread>
#include <QMetaObject>
#include <QObject>

class ProgressRunner : public QObject {
    Q_OBJECT
public:
    using OnProgress = std::function<void(double percent)>;
    using WorkFn =
        std::function<void(const std::atomic_bool& cancel, const OnProgress& onProgress)>;

    explicit ProgressRunner(QObject* parent = nullptr);
    ~ProgressRunner() override;

    // 开始执行；同一时间只允许一个操作。繁忙时返回 false。
    bool start(WorkFn work);
    bool isRunning() const { return running_.load(); }
    void requestCancel() { cancel_.store(true); }

signals:
    void progress(int percent);          // 0~100
    void finished(bool ok, const QString& error);  // ok=false 时 error 为错误或"已取消"

private:
    void run(WorkFn work);

    std::thread thread_;
    std::atomic_bool running_{ false };
    std::atomic_bool cancel_{ false };
};
