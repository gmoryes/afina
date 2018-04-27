#include <afina/coroutine/Engine.h>
#include <algorithm>
#include <cmath>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    volatile long high;

    // Get up bound of stack
    ctx.Hight = reinterpret_cast<unsigned long>(&high);

    std::cout << "Hight = " << ctx.Hight << std::endl;

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

    // Copy stack per byte
    bool simple = false;
    if (simple) {
        unsigned long cur_pointer = ctx.Low;
        int i = 0;
        while (cur_pointer != ctx.Hight) {
            //std::cout << "copy(" << cur_pointer << ")" << std::endl;
            stack[i] = *(char *)(cur_pointer);
            cur_pointer--;
            i++;
        }
    } else {
        std::cout << "Copy from(" << ctx.Hight << ") to(" << ctx.Low << ") size = " << new_size << std::endl;
        std::cout << "stack addr = " << (unsigned long)stack << std::endl;
        std::memcpy(stack, (void*)ctx.Hight, new_size);
    }

    std::cout << "store end" << std::endl;

    ctx.Stack = std::make_tuple(stack, new_size);
}

int d = 100;
void Engine::Restore(context *ctx) {
    if (d) {
        d--;
        Restore(ctx);
    }
    d = 100;
    unsigned long cur_static = ctx->Low;
//    if (ctx->Hight <= (unsigned long)(&cur_static) && (unsigned long)(&cur_static) <= ctx->Low) {
//        /*
//         * This function can be lie between ctx.Low and ctx.Hight, so we use recursion
//         * to skip this place and restore stack safely
//        */
//        Restore(ctx);
//    }

    std::cout << "cur_static(" << (unsigned long)(&cur_static) << ")" << std::endl;
    // Just restore stack per byte
    char *stack;
    unsigned long allocated_memory;
    std::tie(stack, allocated_memory) = ctx->Stack;
    bool simple = false;
    if (simple) {
        unsigned long i = allocated_memory - 1;
        while (cur_static != ctx->Hight) {
            //std::cout << ctx->Low << " " << cur_static << " " << ctx->Hight << std::endl;
            *(char *) cur_static = stack[i];
            //std::cout << "done" << std::endl;
            cur_static--;
            i--;
        }
    } else {
        std::memcpy((void*)ctx->Hight, stack, allocated_memory);
    }
    std::cout << "after copy" << std::endl;
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
        std::cout << "here3" << std::endl;
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