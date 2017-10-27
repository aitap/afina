#include "afina/Executor.h"

namespace Afina {

void perform(Afina::Executor *executor) {
    // not just the lock_guard because we use CVs and will unlock the mutex while user function is running
    std::unique_lock<std::recursive_mutex> lock{mutex};
    // if queue is empty, wait for it to be filled or for the pool to be drained
    empty_condition.wait(lock, [&tasks, &state] { tasks.size() || state != State::kRun });
    if (state != State::kRun)
        return; // abandon ship!
    // TODO
    throw;
}

Executor::Executor(std::string /* wtf?! */, int size) {
    std::lock_guard<std::recursive_mutex> lock{mutex};
    for (int i = 0; i < size; i++)
        threads.emplace(threads.end(), perform, this);
}

void Executor::Stop(bool await) {
    std::lock_guard<std::recursive_mutex> lock{mutex};
    state = State::kStopping;
    // if we're not asked to wait for it to stop, the job is done
    if (await) {
        // otherwise, wake up everyone and force them to commit sudoku
        lock.unlock(); // don't deadlock, allow the workers to read state
        empty_condition.notify_all();
        for (std::thread &thread : threads)
            thread.join();
        lock.lock(); // why do I bother? there's noone here
        state = State::kStopped;
    }
}

Executor::~Executor() {
    std::lock_guard<std::recursive_mutex> lock{mutex};
    if (state == State::kRun)
        Stop(true);
    if (state == State::kStopping) {
        lock.unlock(); // allow the workers to read the state variable
        empty_condition.notify_all();
        for (std::thread &thread : threads)
            thread.join();
    }
    // else we're already stopped, but no need to set that state - we're being destroyed
}
}
