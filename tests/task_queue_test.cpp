#include "TaskQueue.h"
#include "UpdateDialog.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <atomic>
#include <cstdio>

namespace {
int failures = 0;
void check(bool result, const char* label) {
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", label);
    if (!result) ++failures;
}
void wait(TaskQueue& queue) {
    QEventLoop loop;
    QObject::connect(&queue, &TaskQueue::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, [&] { queue.cancelAll(); loop.quit(); });
    loop.exec();
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        TaskQueue queue;
        std::atomic_int active{0};
        std::atomic_int peak{0};
        for (int index = 0; index < 3; ++index) {
            TaskQueue::Task task;
            task.title = QString::number(index);
            task.work = [&](const std::atomic_bool&, const ProgressRunner::OnProgress& progress) {
                const int count = ++active;
                peak.store(std::max(peak.load(), count));
                QThread::msleep(20);
                progress(50);
                --active;
                ggzip::OperationSummary result;
                result.completed = 1;
                return result;
            };
            check(queue.add(task), "queue accepts pending work");
        }
        queue.start(true);
        check(!queue.add({}), "queue rejects overlapping mutation");
        wait(queue);
        check(peak.load() == 1 && queue.tasks()[2].state == TaskQueue::State::Completed,
              "queue runs strictly serially and completes each item");
        TaskQueue cancelled;
        TaskQueue::Task longTask;
        longTask.work = [](const std::atomic_bool& cancel, const ProgressRunner::OnProgress&) {
            while (!cancel.load()) QThread::msleep(5);
            ggzip::OperationSummary result;
            result.outcome = ggzip::Outcome::Cancelled;
            return result;
        };
        cancelled.add(longTask);
        TaskQueue::Task shortTask;
        shortTask.work = [](const std::atomic_bool&, const ProgressRunner::OnProgress&) { return ggzip::OperationSummary{}; };
        cancelled.add(shortTask);
        cancelled.start(true);
        QTimer::singleShot(30, &cancelled, [&] { cancelled.cancel(0); });
        wait(cancelled);
        check(cancelled.tasks()[0].state == TaskQueue::State::Cancelled &&
              cancelled.tasks()[1].state == TaskQueue::State::Completed, "single-item cancellation does not cancel the next task");
        TaskQueue failing;
        TaskQueue::Task bad;
        bad.work = [](const std::atomic_bool&, const ProgressRunner::OnProgress&) -> ggzip::OperationSummary {
            throw std::runtime_error("expected");
        };
        failing.add(bad);
        failing.add(shortTask);
        failing.start(false);
        wait(failing);
        check(failing.tasks()[0].state == TaskQueue::State::Failed &&
              failing.tasks()[1].state == TaskQueue::State::Pending, "stop-on-error leaves remaining tasks pending");
        failing.start(true);
        wait(failing);
        check(failing.tasks()[1].state == TaskQueue::State::Completed, "stopped queue can explicitly resume");
        TaskQueue all;
        all.add(longTask);
        all.add(shortTask);
        all.start(true);
        QTimer::singleShot(30, &all, [&] { all.cancelAll(); });
        wait(all);
        check(all.tasks()[0].state == TaskQueue::State::Cancelled &&
              all.tasks()[1].state == TaskQueue::State::Cancelled, "queue-wide cancellation cancels pending and active work");
        QJsonObject release{{"tag_name", "v0.7.0"}, {"draft", false}, {"prerelease", false},
            {"html_url", "https://github.com/liuyb4016/ggZip/releases/tag/v0.7.0"}, {"body", "Release notes"}};
        auto parse = [&] { return parseUpdate(QJsonDocument(release).toJson(), "0.4.0"); };
        check(parse().newer && parse().version == "0.7.0", "manual update parser compares stable versions");
        release["html_url"] = "https://evil.example/download.exe";
        bool rejected = false;
        try { parse(); } catch (...) { rejected = true; }
        check(rejected, "update parser rejects unexpected download origins");
        release["html_url"] = "https://github.com/liuyb4016/ggZip/releases/tag/v0.7.0";
        release["prerelease"] = true;
        rejected = false;
        try { parse(); } catch (...) { rejected = true; }
        check(rejected, "unsigned prereleases are not offered as stable updates");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return failures ? 1 : 0;
}
