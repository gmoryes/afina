#ifndef AFINA_COROUTINE_ENGINE_H
#define AFINA_COROUTINE_ENGINE_H

#include <cstdint>
#include <iostream>
#include <map>
#include <setjmp.h>
#include <tuple>
#include <logger/Logger.h>

namespace Afina {
namespace Coroutine {

/**
 * A single coroutine instance which could be scheduled for execution
 * should be allocated on heap
 */
struct context {
    // coroutine stack start address
    unsigned long Low = 0;

    // coroutine stack end address
    unsigned long Hight = 0;

    // coroutine stack copy buffer
    std::tuple<char*, unsigned long> Stack = std::make_tuple(nullptr, 0);

    // Saved coroutine context (registers)
    jmp_buf Environment;

    // To include routine in the different lists, such as "alive", "blocked", e.t.c
    struct context *prev = nullptr;
    struct context *next = nullptr;
};

/**
 * # Entry point of coroutine library
 * Allows to run coroutine and schedule its execution. Not threadsafe
 */
class Engine final {
private:
    /**
     * Where coroutines stack begins
     */
    unsigned long StackBottom;

    /**
     * Current coroutine
     */
    context *cur_routine;

    /**
     * List of routines ready to be scheduled. Note that suspended routine ends up here as well
     */
    context *alive;

    /**
     * Context to be returned finally
     */
    context *idle_ctx{};

protected:
    /**
     * Save stack of the current coroutine in the given context
     */
    void Store(context &ctx);

    /**
     * Restore stack of the given context and pass control to coroutinne
     */
    void Restore(context *ctx);

    /**
     * Suspend current coroutine execution and execute given context
     */
    // void Enter(context& ctx);

public:
    Engine() : StackBottom(0), cur_routine(nullptr), alive(nullptr) {}
    Engine(Engine &&) = delete;
    Engine(const Engine &) = delete;

    /**
     * Gives up current routine execution and let engine to schedule other one. It is not defined when
     * routine will get execution back, for example if there are no other coroutines then executing could
     * be trasferred back immediately (yield turns to be noop).
     *
     * Also there are no guarantee what coroutine will get execution, it could be caller of the current one or
     * any other which is ready to run
     */
    void yield();

    /**
     * Suspend current routine and transfers control to the given one, resumes its execution from the point
     * when it has been suspended previously.
     *
     * If routine to pass execution to is not specified runtime will try to transfer execution back to caller
     * of the current routine, if there is no caller then this method has same semantics as yield
     */
    void sched(context *routine);

    /**
     * Entry point into the engine. Prepare all internal mechanics and starts given function which is
     * considered as main.
     *
     * Once control returns back to caller of start all coroutines are done execution, in other words,
     * this function doesn't return control until all coroutines are done.
     *
     * @param pointer to the main coroutine
     * @param arguments to be passed to the main coroutine
     */
    template <typename... Ta> void start(void (*main)(Ta...), Ta &&... args) {
        // To acquire stack begin, create variable on stack and remember its address
        volatile long StackStartsHere;
        this->StackBottom = reinterpret_cast<unsigned long>(&StackStartsHere);

        // Start routine execution
        context *pc = run(main, std::forward<Ta>(args)...);
        std::cout << "here1" << std::endl;
        if (pc == nullptr)
            std::runtime_error("Engine::run() return nullptr");

        std::cout << "here2" << std::endl;

        idle_ctx = new context();
        idle_ctx->Low = this->StackBottom;
        cur_routine = idle_ctx;

        alive = pc;

        if (setjmp(idle_ctx->Environment) > 0) {
            // Here: correct finish of the coroutine section
            yield();
        } else {
            Store(*idle_ctx);
            sched(pc);
        }
        // Shutdown runtime
        delete idle_ctx;
        this->StackBottom = 0;
    }

    /**
     * @return Return the next alive coroutine
     */
    context *get_next_coroutine();

    /**
     * Exit from current coroutine, and switch to the next alive coroutine
     * Never return from if (!)
     */
    void exit_from_coroutine();

    /**
     * Register new coroutine. It won't receive control until scheduled explicitely or implicitly. In case of some
     * errors function returns -1
     */
    template <typename... Ta>
    context *run(void (*func)(Ta...), Ta &&... args) {
        if (this->StackBottom == 0) {
            // Engine wasn't initialized yet
            return nullptr;
        }

        int x = 123123123;
        /* debug */
        Logger& logger = Logger::Instance();

        // New coroutine context that carries around all information enough to call function
        context *pc = new context();
        pc->Low = StackBottom;

        /** Store current state right here, i.e just before enter new coroutine, later, once it gets scheduled
         * execution starts here. Note that we have to acquire stack of the current function call to ensure
         * that function parameters will be passed along
         */
        if (setjmp(pc->Environment) > 0) {
            /**
             * Created routine got control in order to start execution. Note that all variables, such as
             * context pointer, arguments and a pointer to the function comes from restored stack
             */
            std::cout << "x = " << x << std::endl;
            // invoke routine
            func(std::forward<Ta>(args)...);

            /** Routine has completed its execution, time to delete it. Note that we should be extremely careful in where
             * to pass control after that. We never want to go backward by stack as that would mean to go backward in
             * time. Function run() has already return once (when setjmp returns 0), so return second return from run
             * would looks a bit awkward
             */
            if (pc->prev != nullptr) {
                pc->prev->next = pc->next;
            }

            if (pc->next != nullptr) {
                pc->next->prev = pc->prev;
            }

            if (alive == cur_routine) {
                alive = alive->next;
            }

            // current coroutine finished, and the pointer is not relevant now
            cur_routine = nullptr;
            pc->prev = pc->next = nullptr;
            delete std::get<0>(pc->Stack);
            delete pc;

            /** We cannot return here, as this function "returned" once already, so here we must select some other
             * coroutine to run. As current coroutine is completed and can't be scheduled anymore, it is safe to
             * just give up and ask scheduler code to select someone else, control will never returns to this one
             */
            Engine::exit_from_coroutine();
        }

        /** setjmp remembers position from which routine could starts execution, but to make it correctly
         * it is necessary to save arguments, pointer to body function, pointer to context, e.t.c - i.e
         * save stack.
         */
        Store(*pc);

        // Add routine as alive double-linked list
        pc->next = alive;
        alive = pc;
        if (pc->next != nullptr) {
            pc->next->prev = pc;
        }

        return pc;
    }
};

} // namespace Coroutine
} // namespace Afina

#endif // AFINA_COROUTINE_ENGINE_H
