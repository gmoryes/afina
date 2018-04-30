#include <afina/coroutine/Engine.h>
#include <algorithm>
#include <cmath>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    volatile long high;

    // Get up bound of stack
    ctx.Hight = reinterpret_cast<unsigned long>(&high);

    unsigned long allocated_memory;
    char *stack;
    std::tie(stack, allocated_memory) = ctx.Stack;

    unsigned long new_size = ctx.Low - ctx.Hight;
    if (new_size > allocated_memory) {
        // Allocate more memory if need
        if (stack)
            delete[] stack;
        stack = new char[new_size];
    }

    std::memcpy(stack, (void*)ctx.Hight, new_size);

    ctx.Stack = std::make_tuple(stack, new_size);
}

void Engine::Restore(context *ctx) {
    unsigned long cur_static = ctx->Low;
    if (ctx->Hight <= (unsigned long)(&cur_static) && (unsigned long)(&cur_static) <= ctx->Low) {
        /*
         * This function can be lie between ctx.Low and ctx.Hight, so we use recursion
         * to skip this place and restore stack safely
        */
        Restore(ctx);
    }

    // Just restore stack per byte
    char *stack;
    unsigned long allocated_memory;
    std::tie(stack, allocated_memory) = ctx->Stack;
    std::memcpy((void*)ctx->Hight, stack, allocated_memory);

    // Make jump
    cur_routine = ctx;
    longjmp(ctx->Environment, 1);
}

context *Engine::get_next_coroutine() {
    context *result;

    if (cur_routine == nullptr) {
        if (alive) {
            result = alive;
        } else {
            result = idle_ctx;
        }
    } else {
        if (cur_routine->next) {
            // If we has the next routine in queue, get it
            result = cur_routine->next;
        } else if (alive) {
            // If current routine was the last, let go to the begin of queue
            result = alive;
        } else {
            // If there are no routines, jump into idle_ctx
            result = idle_ctx;
        }
    }

    return result;
}

void Engine::yield() {
    sched(
        Engine::get_next_coroutine()
    );
}

void Engine::sched(context *routine_) {
    if (setjmp(cur_routine->Environment) > 0) {
        return;
    } else {
        Store(*cur_routine);
        Restore(routine_);
    }

}

void Engine::exit_from_coroutine() {
    context *next_coro = Engine::get_next_coroutine();
    Restore(next_coro);
}

} // namespace Coroutine
} // namespace Afina