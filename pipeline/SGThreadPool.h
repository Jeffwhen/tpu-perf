#ifndef SGTHREADPOOL_H
#define SGTHREADPOOL_H

#include<vector>
#include<thread>
#include<memory>
#include<atomic>
#include<future>
#include "SGQueue.h"


namespace bm {

class SGTask: public Uncopiable {
public:
    SGTask() = default;

    template<typename FuncType>
    SGTask(FuncType&& f):
        imp(new ImpType<FuncType>(std::move(f))){}

    SGTask(SGTask&& other):
        imp(std::move(other.imp)) {}

    SGTask& operator = (SGTask&& other) {
        imp = std::move(other.imp);
        return *this;
    }

    void operator() () { imp->call(); }

private:
    struct ImpBase{
        virtual void call() = 0;
        virtual ~ImpBase() {};
    };
    template<typename FuncType>
    struct ImpType: ImpBase {
        FuncType f;
        ImpType(FuncType&& f_): f(std::move(f_)){};
        void call() override { f(); };
        virtual ~ImpType() {}
    };

    std::unique_ptr<ImpBase> imp;
};

class SGThreadPool: public Uncopiable
{
private:
    using Threads = std::vector<std::thread>;
    class __ThreadsJoiner {
        public:
        __ThreadsJoiner(Threads& ts);
        ~__ThreadsJoiner();
        void join();
        private:
        Threads& threads;
    };

    bool popLocalWork(SGTask& task);
    bool popGlobalWork(SGTask& task);
    bool stealOtherWork(SGTask& task);

    void workThread(size_t index);
    void runPendingTask();

    std::atomic_bool done;
    SGQueue<SGTask> globalQueue;
    std::vector<std::unique_ptr<SGWorkStealingQueue<SGTask>>> allLocalQueues;

    static thread_local SGWorkStealingQueue<SGTask>* localQueue;
    static thread_local size_t threadIndex;

    Threads threads;
    __ThreadsJoiner joiner;

public:
    SGThreadPool(size_t num_thread =1);
    ~SGThreadPool();

    template<typename FuncType, typename ... ArgTypes>
    std::future<typename std::result_of<FuncType(ArgTypes...)>::type> submit(FuncType f, ArgTypes... args){
        using ResultType = typename std::result_of<FuncType(ArgTypes...)>::type;
        auto func = [f, args...] () { return f(args...); };
        std::packaged_task<ResultType()> task(func);
        auto res = task.get_future();
        if(localQueue){
            localQueue->push(std::move(task));
        } else {
            globalQueue.push(std::move(task));
        }
        return res;
    }
};

}

#endif // SGTHREADPOOL_H
