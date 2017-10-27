#include "afina/Executor.h"

namespace Afina {

void perform(Afina::Executor *executor) { throw; }

Executor::Executor(std::string /* wtf?! */, int size) {
    std::lock_guard<std::recursive_mutex> lock{mutex};
    for (int i = 0; i < size; i++)
        threads.emplace(threads.end(), perform, this);
}

void Executor::Stop(bool await) {
    std::lock_guard<std::recursive_mutex> lock{mutex};
    state = State::kStopping;
    if (await) {
        for (std::thread &thread : threads)
            thread.join();
        state = State::kStopped;
    }
}

Executor::~Executor() {
    std::lock_guard<std::recursive_mutex> lock{mutex};
    if (state == State::kRun)
        Stop(true);
    if (state == State::kStopping)
        for (std::thread &thread : threads)
            thread.join();
}
}
