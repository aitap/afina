#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

/*
 * Just so I don't forget:
 * the stack grows DOWNWARDS.
 */

void Engine::Store(context &ctx) {
    volatile char onmystack = 0;
    std::get<1>(ctx.Stack) = StackBottom - &onmystack;
    std::get<0>(ctx.Stack) = new char[std::get<1>(ctx.Stack)];
    memcpy(std::get<0>(ctx.Stack), (const void *)&onmystack, std::get<1>(ctx.Stack));
    // that's all we needed to do
}

void Engine::Restore(context &ctx) {
    volatile unsigned char onmystack = 0;
    // don't clobber the state by function call
    if ((char *)&onmystack > StackBottom - std::get<1>(ctx.Stack)) {
        Restore(ctx); // although this doesn't return
        onmystack++;  // tell the compiler NOT to optimize the tail-call
        return;
    }
    // restore the stack
    memcpy(StackBottom, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    // jump to the frame
    return longjmp(ctx.Environment, 1);
    // Engine::run will clean up for us
}

void Engine::yield() {}

void Engine::sched(void *routine_) {
    context *to_call = (context *)routine_;
    context *caller = new context;
    Store(*caller);
    if (setjmp(caller->Environment)) {
        // we got our stack restored and called back
        // clean up and get on with our work
        delete[] std::get<0>(caller.Stack);
        delete caller;
        return;
    }
    // FIXME: else what?
    // add to some queue, I guess
}

} // namespace Coroutine
} // namespace Afina
