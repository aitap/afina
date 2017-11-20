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
    // other methods use this field, so set it
    cur_routine = &ctx;
    // restore the stack, starting at the deepest part of the stack (lowest address)
    memcpy(StackBottom - std::get<1>(ctx.Stack), std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    // jump to the frame
    return longjmp(ctx.Environment, 1); // actually doesn't return
}

void Engine::yield() {
    if (alive) {
        context *to_call = alive;
        alive = alive->next;
        alive->prev = nullptr;
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
        cur_routine->next = alive;

        alive = cur_routine;
        if (cur_routine->next) {
            cur_routine->next->prev = cur_routine;
        }

        if (setjmp(cur_routine->Environment)) {
            // we got our stack restored and called back
            return;
        }
    }
    cur_routine = to_call;
    Restore(*to_call);
}

} // namespace Coroutine
} // namespace Afina
