#include<algorithm>
#include "SGThreadPool.h"
#include "SGLog.h"

namespace bm {

bool SGThreadPool::popLocalWork(SGTask &task)
{
    return localQueue && localQueue->tryPop(task);
}

bool SGThreadPool::popGlobalWork(SGTask &task)
{
    return globalQueue.tryPop(task);
}

bool SGThreadPool::stealOtherWork(SGTask &task)
{
    size_t numThread = allLocalQueues.size();
    for(size_t i=0; i<numThread; i++){
        size_t currentIndex = (threadIndex+i+1)%numThread;
        if(allLocalQueues[currentIndex]->trySteal(task)){
            return true;
        }
    }
    return false;
}

void SGThreadPool::workThread(size_t index) {
    threadIndex = index;
    localQueue = allLocalQueues[threadIndex].get();
    SGLOG(DEBUG, "begin thread id=%d, index=%d", std::this_thread::get_id(), threadIndex);
    while (!done) {
        runPendingTask();
    }
    SGLOG(DEBUG, "end thread id=%d", std::this_thread::get_id());
}

void SGThreadPool::runPendingTask()
{
        SGTask task;
        if(globalQueue.tryPop(task)) {
            SGLOG(DEBUG, "[%d] get a task", std::this_thread::get_id());
            task();
        } else {
            std::this_thread::yield();
        }
}

SGThreadPool::SGThreadPool(size_t num_thread): done(false), joiner(threads) {
    localQueue = nullptr;
    try {
        for(size_t i = 0; i<num_thread; i++){
            allLocalQueues.emplace_back(new SGWorkStealingQueue<SGTask>);
            threads.emplace_back(&SGThreadPool::workThread, this, i);
            SGLOG(DEBUG, "create thread id=%d", threads.back().get_id());
        }
    } catch(...){
        done = true;
        throw;
    }
}

SGThreadPool::~SGThreadPool(){
    done = true;
}

SGThreadPool::__ThreadsJoiner::__ThreadsJoiner(SGThreadPool::Threads &ts): threads(ts) {}

void SGThreadPool::__ThreadsJoiner::join(){
    std::for_each(threads.begin(), threads.end(), [](std::thread& t){
        if(t.joinable()){
            SGLOG(DEBUG, "join thread id=%d", t.get_id());
            t.join();
        }
    });
}

SGThreadPool::__ThreadsJoiner::~__ThreadsJoiner(){
    join();
}
thread_local SGWorkStealingQueue<SGTask>* SGThreadPool::localQueue = nullptr;
thread_local size_t SGThreadPool::threadIndex = 0;

}
