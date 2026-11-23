#include "ProgressRunner.h"
#include "ggzip/ArchiveError.h"

ProgressRunner::ProgressRunner(QObject* parent) : QObject(parent) {}

ProgressRunner::~ProgressRunner() {
    requestCancel();
    if (thread_.joinable()) thread_.join();
}

bool ProgressRunner::start(WorkFn work) {
    if (running_.load()) return false;
    if (thread_.joinable()) thread_.join();
    cancel_.store(false);
    running_.store(true);
    thread_ = std::thread([this, w = std::move(work)]() mutable {
        run(std::move(w));
    });
    return true;
}

void ProgressRunner::run(WorkFn work) {
    QString err;
    bool ok = true;
    try {
        work(cancel_, [this](double pct) {
            QMetaObject::invokeMethod(
                this, [this, pct] { emit progress((int)pct); },
                Qt::QueuedConnection);
        });
    } catch (const std::exception& e) {
        ok = false;
        err = QString::fromUtf8(e.what());
    }
    const bool cancelled = cancel_.load();
    if (cancelled) err.clear();
    QMetaObject::invokeMethod(
        this,
        [this, ok, err, cancelled] {
            running_.store(false);
            emit finished(ok && !cancelled, err);
        },
        Qt::QueuedConnection);
}
