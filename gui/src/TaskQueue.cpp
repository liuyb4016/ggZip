#include "TaskQueue.h"
#include "ggzip/OperationError.h"
#include <QTimer>

TaskQueue::TaskQueue(QObject* parent) : QObject(parent) {
    connect(&runner_, &ProgressRunner::progress, this, [this](int percent) {
        if (active_ < tasks_.size()) tasks_[active_].percent = percent;
        emit changed();
    });
    connect(&runner_, &ProgressRunner::finished, this, [this](bool ok, const QString& error) {
        auto& task = tasks_[active_];
        task.result = pendingResult_;
        task.error = error;
        task.state = cancelled_ || task.result.outcome == ggzip::Outcome::Cancelled ? State::Cancelled :
            ok && task.result.outcome == ggzip::Outcome::Completed ? State::Completed : State::Failed;
        if (task.state == State::Completed) task.percent = 100;
        if (task.state == State::Failed && !continueOnError_) enabled_ = false;
        task.work = {};
        emit changed();
        QTimer::singleShot(0, this, &TaskQueue::next);
    });
}

bool TaskQueue::add(Task task) {
    if (busy() || enabled_ || !task.work) return false;
    tasks_.push_back(std::move(task));
    emit changed();
    return true;
}

TaskQueue::~TaskQueue() { runner_.requestCancel(); runner_.wait(); }

void TaskQueue::start(bool continueOnError) {
    if (busy()) return;
    continueOnError_ = continueOnError;
    enabled_ = true;
    next();
}

void TaskQueue::next() {
    if (busy()) return;
    if (enabled_) {
        for (std::size_t index = 0; index < tasks_.size(); ++index) {
            if (tasks_[index].state != State::Pending) continue;
            active_ = index;
            cancelled_ = false;
            pendingResult_ = {};
            tasks_[index].state = State::Running;
            const auto work = tasks_[index].work;
            runner_.start([this, work](const std::atomic_bool& cancel, const ProgressRunner::OnProgress& progress) {
                try { pendingResult_ = work(cancel, progress); }
                catch (const ggzip::OperationError& error) { pendingResult_ = error.summary; throw; }
            });
            emit changed();
            return;
        }
    }
    enabled_ = false;
    emit idle();
}

void TaskQueue::cancel(std::size_t index) {
    if (index >= tasks_.size()) return;
    auto& task = tasks_[index];
    if (task.state == State::Pending) { task.state = State::Cancelled; task.work = {}; }
    else if (task.state == State::Running) { cancelled_ = true; runner_.requestCancel(); }
    emit changed();
}

void TaskQueue::cancelAll() {
    enabled_ = false;
    for (std::size_t index = 0; index < tasks_.size(); ++index) cancel(index);
    if (!busy()) emit idle();
}
