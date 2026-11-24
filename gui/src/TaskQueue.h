#pragma once
#include "ProgressRunner.h"
#include "ggzip/IEngine.h"
#include <QObject>
#include <vector>

class TaskQueue : public QObject {
    Q_OBJECT
public:
    enum class State { Pending, Running, Completed, Cancelled, Failed };
    using Work = std::function<ggzip::OperationSummary(const std::atomic_bool&, const ProgressRunner::OnProgress&)>;
    struct Task {
        QString title;
        QString target;
        Work work;
        State state = State::Pending;
        int percent = 0;
        QString error;
        ggzip::OperationSummary result;
    };
    explicit TaskQueue(QObject* parent = nullptr);
    ~TaskQueue() override;
    bool add(Task task);
    void start(bool continueOnError);
    void cancel(std::size_t index);
    void cancelAll();
    bool busy() const { return runner_.isRunning(); }
    const std::vector<Task>& tasks() const { return tasks_; }
signals:
    void changed();
    void idle();
private:
    void next();
    ProgressRunner runner_;
    std::vector<Task> tasks_;
    std::size_t active_ = 0;
    bool enabled_ = false;
    bool continueOnError_ = false;
    ggzip::OperationSummary pendingResult_;
    bool cancelled_ = false;
};
