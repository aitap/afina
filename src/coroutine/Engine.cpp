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
    if (std::get<0>(ctx.Stack))
        delete[] std::get<0>(ctx.Stack);
    std::get<0>(ctx.Stack) = new char[std::get<1>(ctx.Stack)];
    memcpy(std::get<0>(ctx.Stack), (const void *)&onmystack, std::get<1>(ctx.Stack));
    // that's all we needed to do
}

void Engine::Restore(context &ctx) {
    volatile unsigned char onmystack = 0;
    // don't clobber the state by function call
    if ((char *)&onmystack > StackBottom - std::get<1>(ctx.Stack)) {
        volatile uint64_t filler = 0;
        Restore(ctx); // this doesn't return, but don't let the compiler know about it
        filler++;
    }
    // restore the stack, starting at the deepest part of the stack (lowest address)
    memcpy(StackBottom - std::get<1>(ctx.Stack), std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    // jump to the frame
    return longjmp(ctx.Environment, 1); // doesn't return
}

void Engine::yield() {
    if (alive) {
        context *to_call = alive;
        alive->prev = nullptr;
        alive = alive->next;
        return sched(to_call);
    }
}

// cur_routine is the caller, we should land in it when the callee returns
// unless sched is called from run() or start(), then it's NULL
void Engine::sched(void *routine_) {
    context *to_call = (context *)routine_;
    to_call->caller = cur_routine;
    if (cur_routine) {
        cur_routine->callee = to_call;
        Store(*cur_routine); // so there would be a place to return to
        if (setjmp(cur_routine->Environment)) {
            return;
        }
    }
    // else we should land in the idle context
    cur_routine = to_call;
    Restore(*to_call);
}

} // namespace Coroutine
} // namespace Afina
