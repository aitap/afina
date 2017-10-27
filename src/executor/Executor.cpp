#include "afina/Executor.h"

namespace Afina {

void Executor::perform() {
    // not just the lock_guard because we use CVs and will unlock the mutex while user function is running
    std::unique_lock<std::mutex> lock{mutex};
    for (;;) {
        // if queue is empty, wait for it to be filled or for the pool to be drained
        empty_condition.wait(lock, [this] { return tasks.size() || state != State::kRun; });
        if (state != Executor::State::kRun)
            return; // abandon ship!
        auto f = std::move(tasks.front());
        tasks.pop_front();
        lock.unlock(); // don't need that while user code is running
        f();
        lock.lock();
    }
}

Executor::Executor(std::string /* wtf?! */, int size) {
    std::lock_guard<std::mutex> lock{mutex};
    state = State::kRun;
    for (int i = 0; i < size; i++)
        threads.emplace(threads.end(), &Executor::perform, this);
}

void Executor::await_unlocked(std::unique_lock<std::mutex> &lock) {
    lock.unlock(); // don't deadlock, allow the workers to read state
    empty_condition.notify_all();
    for (std::thread &thread : threads)
        thread.join();
    lock.lock(); // why do I bother? there's noone here
}

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> lock{mutex};
    state = State::kStopping;
    // if we're not asked to wait for it to stop, the job is done
    if (await) {
        // otherwise, wake up everyone and force them to commit sudoku
        await_unlocked(lock);
        state = State::kStopped;
    }
}

Executor::~Executor() {
    std::unique_lock<std::mutex> lock{mutex};
    if (state == State::kRun)
        state = State::kStopping;
    if (state == State::kStopping)
        await_unlocked(lock);
}
}
