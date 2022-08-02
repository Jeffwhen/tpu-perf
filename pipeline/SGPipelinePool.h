#ifndef SGPIPELINEPOOL_H
#define SGPIPELINEPOOL_H
#include <string>
#include <set>
#include <vector>
#include <memory>
#include <future>
#include <thread>
#include <typeinfo>
#include <type_traits>
#include "SGLog.h"
#include "SGCommonUtils.h"
#include "SGQueue.h"

namespace bm {

class SGPipelineNodeBase {
private:
public:
    virtual ~SGPipelineNodeBase() {}
    virtual void start() = 0;
    virtual void join(bool join_out_queue = false) = 0;
    virtual void setOutQueue(std::shared_ptr<SGQueueVoid>) = 0;
};

struct SGPipelineEmptyContext { };

template<typename InType, typename OutType, typename ContextType = SGPipelineEmptyContext>
class SGPipelineNodeImp: public Uncopiable, public SGPipelineNodeBase {
private:
    using InQueuePtr=std::shared_ptr<SGQueue<InType>>;
    using OutQueuePtr=std::shared_ptr<SGQueue<OutType>>;
    using TaskType =  std::function<bool (const InType&, OutType&, std::shared_ptr<ContextType>)>;

    std::shared_ptr<ContextType> context;
    TaskType taskFunc;
    InQueuePtr inFreeQueue;
    InQueuePtr inTaskQueue;
    OutQueuePtr outFreeQueue;
    OutQueuePtr outTaskQueue;
    std::thread innerThread;
    std::atomic_bool& done;
    std::string name;

    void workThread(){
        if(!inTaskQueue) {
            done = true;
            SGLOG(FATAL, "[%s] no input task queue!", name.c_str());
            return;
        }
        // done is global, if it is set all pipeline threads will stop
        // so we use a seperate local flag
        // done can still be useful, such as when handling exceptions
        bool join = false;
        while (!done && !join){
            OutType out;
            InType in;
            if(outFreeQueue && outFreeQueue->waitAndPop(out)) {
                SGLOG(DEBUG, "[%s] got an output resource", name.c_str());
            }
            bool finish = false;
            while(!done && !finish){
                if(inTaskQueue->waitAndPop(in)) {
                    SGLOG(DEBUG, "[%s] got a task", name.c_str());
                } else {
                    SGLOG(DEBUG, "[%s] join", name.c_str());
                    join = true;
                    break;
                }
                if(!done){
                    finish = taskFunc(in, out, context);
                    if(inFreeQueue) {
                        SGLOG(DEBUG, "[%s] return an input resource", name.c_str());
                        inFreeQueue->push(in);
                    }
                } else {
                    break;
                }
            }
            if (join) {
                break;
            }
            if(!done){
                if(outTaskQueue) {
                    SGLOG(DEBUG, "[%s] put a task", name.c_str());
                    outTaskQueue->push(out);
                }
            }
        }
        SGLOG(DEBUG, "[%s] leave thread", name.c_str());
    }

public:
    SGPipelineNodeImp(TaskType taskFunc,
                      InQueuePtr inFreeQueue, InQueuePtr inTaskQueue,
                      OutQueuePtr outFreeQueue, OutQueuePtr outTaskQueue,
                      std::atomic_bool& done,
                      std::shared_ptr<ContextType> context,
                      const std::string& name
                      ):
        taskFunc(taskFunc),
        inFreeQueue(inFreeQueue), inTaskQueue(inTaskQueue),
        outFreeQueue(outFreeQueue), outTaskQueue(outTaskQueue),
        done(done),
        context(context), name(name)
    {}

    virtual void setOutQueue(std::shared_ptr<SGQueueVoid> outQueueVoid) override {
        auto outQueue = std::dynamic_pointer_cast<SGQueue<OutType>>(outQueueVoid);
        if(!outQueue){
            SGLOG(FATAL, "output queue set failed");
        }
        outTaskQueue = outQueue;
    }

    void start() override {
        innerThread = std::thread(&SGPipelineNodeImp<InType, OutType, ContextType>::workThread, this);
        SGLOG(DEBUG, "thread created id=%d", innerThread.get_id());
    }
    void join(bool join_out_queue = false) override {
        if(innerThread.joinable()){
            innerThread.join();
        }
        if (join_out_queue) {
            outTaskQueue->join();
        }
    }

    virtual ~SGPipelineNodeImp() {
        SGLOG(DEBUG, "thread=%d destructed", innerThread.get_id());
        join();
    }
 };

template<typename InType, typename OutType, typename ContextType=SGPipelineEmptyContext>
class SGPipeline: public Uncopiable {
private:
    std::shared_ptr<SGQueue<InType>> inQueue;
    std::shared_ptr<SGQueue<OutType>> outQueue;
    std::vector<std::shared_ptr<SGPipelineNodeBase>> pipelineNodes;
    std::shared_ptr<ContextType> context;
    std::atomic_bool done;
    std::shared_ptr<SGQueueVoid> lastOutResourceQueue;
    std::shared_ptr<SGQueueVoid> lastOutWorkQueue;
    std::string lastTypeName;
    std::string outTypeName;
    std::string pipelineName;

public:
    SGPipeline(std::shared_ptr<ContextType> context = std::shared_ptr<ContextType>(), const std::string& name="node"):
        context(context),
        done(false),
        pipelineName(name)
    {
        setInputQueue(std::make_shared<SGQueue<InType>>());
        lastOutResourceQueue = std::shared_ptr<SGQueue<InType>>();
        if(!inQueue){
            SGLOG(FATAL, "Cannot create input queue!");
        }
        lastTypeName = typeid(InType).name();
        outTypeName = typeid(OutType).name();
        outQueue = std::shared_ptr<SGQueue<OutType>>();
    }

    void setInputQueue(std::shared_ptr<SGQueue<InType>> inQueue_){
        if(!pipelineNodes.empty()){
            SGLOG(FATAL, "input queue cannot be set after call addNode");
        }
        inQueue = inQueue_;
        lastOutWorkQueue = inQueue;
    }

    void setOutputQueue(std::shared_ptr<SGQueue<OutType>> outQueue){
        if(pipelineNodes.empty()){
            SGLOG(FATAL, "output queue cannot be set after call addNode");
        }
        lastOutWorkQueue = outQueue;
        pipelineNodes.back()->setOutQueue(outQueue);
    }

    template<typename NodeInType, typename NodeOutType, typename Container = std::vector<NodeOutType>>
    void addNode(std::function<NodeOutType(const NodeInType&, std::shared_ptr<ContextType>)> func,
                 Container outResource = {}) {
        std::function<bool(const NodeInType&, NodeOutType&, std::shared_ptr<ContextType>)> inner_func = [func](
                const NodeInType& in, NodeOutType& out, std::shared_ptr<ContextType> ctx){
            out =  std::move(func(in, ctx));
            return true;
        };
        addNode(inner_func, outResource);
    }

    template<typename NodeInType, typename NodeOutType, typename Container = std::vector<NodeOutType>>
    void addNode(std::function<NodeOutType(const NodeInType&)> func,
                 Container outResource = {}) {
        std::function<bool(const NodeInType&, NodeOutType&, std::shared_ptr<ContextType>)> inner_func = [func](
                const NodeInType& in, NodeOutType& out, std::shared_ptr<ContextType>){
            out =  std::move(func(in));
            return true;
        };
        addNode(inner_func, outResource);
    }

    template<typename NodeInType, typename NodeOutType, typename Container = std::vector<NodeOutType>>
    void addNode(std::function<bool(const NodeInType&, NodeOutType&)> func,
                 Container outResource = {}) {
        std::function<bool(const NodeInType&, NodeOutType&, std::shared_ptr<ContextType>)> inner_func = [func](
                const NodeInType& in, NodeOutType& out, std::shared_ptr<ContextType>){ return func(in, out); };
        addNode(inner_func, outResource);
    }

    template<typename NodeInType, typename NodeOutType, typename Container= std::vector<NodeOutType>>
    void addNode(std::function<bool(const NodeInType&, NodeOutType&, std::shared_ptr<ContextType>)> func,
                 Container outResource = {}) {
        auto inWorkQueue = std::dynamic_pointer_cast<SGQueue<NodeInType>>(lastOutWorkQueue);
        if(!inWorkQueue) {
            SGLOG(FATAL, "input type of the added node is wrong: %s is needed, but got %s", lastTypeName.c_str(),typeid(NodeInType).name());
        }
        auto inResourceQueue = std::dynamic_pointer_cast<SGQueue<NodeInType>>(lastOutResourceQueue);

        lastTypeName = typeid(NodeOutType).name();
        lastOutWorkQueue = std::shared_ptr<SGQueue<NodeOutType>>(new SGQueue<NodeOutType>());
        if(!outResource.empty()){
            lastOutResourceQueue = std::shared_ptr<SGQueue<NodeOutType>>(new SGQueue<NodeOutType>());
        } else {
            lastOutResourceQueue =  std::shared_ptr<SGQueueVoid>();
        }
        auto outWorkQueue = std::dynamic_pointer_cast<SGQueue<NodeOutType>>(lastOutWorkQueue);
        auto outResourceQueue = std::dynamic_pointer_cast<SGQueue<NodeOutType>>(lastOutResourceQueue);
        for(auto& out: outResource){
            outResourceQueue->push(out);
        }
        std::string nodeName = pipelineName+"_n" + std::to_string(pipelineNodes.size());
        pipelineNodes.emplace_back(
                    new SGPipelineNodeImp<NodeInType, NodeOutType, ContextType>(func,
                                                                                inResourceQueue, inWorkQueue,
                                                                                outResourceQueue, outWorkQueue,
                                                                                done, context, nodeName)
                    );
    }

    void start() {
        // connect last node
        done = false;
        outQueue = std::dynamic_pointer_cast<SGQueue<OutType>>(lastOutWorkQueue);
        if(!outQueue) {
            SGLOG(FATAL, "output type of the last node is wrong: %s is needed, but got %s", outTypeName.c_str(),lastTypeName.c_str());
        }
        if(lastOutResourceQueue) {
            SGLOG(FATAL, "output of pipeline should not be resource limited!");
        }
        for(auto& node: pipelineNodes){
            node->start();
        }
    }

    std::shared_ptr<ContextType> getContext() const {
        return context;
    }

    void push(InType value){
        inQueue->push(value);
    }

    bool pop(OutType& value){
        if(!outQueue){
            SGLOG(FATAL, "pipeline is not started!");
        }
        return outQueue->tryPop(value);
    }

    bool waitAndPop(OutType &value) {
        return outQueue->waitAndPop(value);
    }

    bool isStopped(){
        return done;
    }

    void join() {
        for (int i = 0; i < pipelineNodes.size(); ++i)
        {
            auto &node = pipelineNodes[i];
            node->join(i + 1 != pipelineNodes.size());
        }
    }

    void stop(){
        done = true;
        this->join();
    }

    ~SGPipeline(){
        SGLOG(DEBUG, "PIPELINE destructed!");
        if(!done) stop();
    }
};

template <typename InType, typename OutType, typename ContextType = SGPipelineEmptyContext>
class SGPipelinePool: public Uncopiable
{
private:
   std::vector<std::unique_ptr<SGPipeline<InType, OutType, ContextType>>> pipelines;
   std::shared_ptr<SGQueue<InType>> inQueue;
   std::shared_ptr<SGQueue<OutType>> outQueue;
   std::function<void(std::shared_ptr<ContextType>)> contextDeinitializer;

public:
    SGPipelinePool(size_t num_pipeline = 1,
                   std::function<std::shared_ptr<ContextType>(size_t)> contextInitializer = nullptr,
                   std::function<void(std::shared_ptr<ContextType>)> contextDeinitializer = nullptr,
                   std::function<std::string(size_t, ContextType &)> nameFunc = nullptr
                   ) {
        inQueue = std::make_shared<SGQueue<InType>>();
        outQueue = std::make_shared<SGQueue<OutType>>();
        for(size_t i=0; i<num_pipeline; i++){
            std::shared_ptr<ContextType> context;
            if(contextInitializer){
                context = contextInitializer(i);
            }
            std::string pipelineName = std::string("pipeline") + std::to_string(i);
            if(nameFunc){
                pipelineName = nameFunc(i, *context);
            }
            pipelines.emplace_back(new SGPipeline<InType, OutType, ContextType>(context, pipelineName));
        }
        for(auto& pipeline: pipelines){
            pipeline->setInputQueue(inQueue);
        }
        this->contextDeinitializer = contextDeinitializer;
    }
 
    const ContextType &getPipeLineContext(int index = 0) const {
        if (index >= pipelines.size())
        {
            SGLOG(FATAL, "invalid index %d in %d", index, pipelines.size());
            throw std::runtime_error("index overflow");
        }
        return *pipelines[index]->getContext();
    }

    std::shared_ptr<SGQueue<InType>> getInputQueue(){
        return inQueue;
    }

    template<typename NodeInType, typename NodeOutType, typename Container = std::vector<NodeOutType>>
    void addNode(std::function<NodeOutType(const NodeInType&)> func,
                 std::function<Container(std::shared_ptr<ContextType>)> outResourceInitializer = nullptr) {
        std::function<bool(const NodeInType&, NodeOutType&, std::shared_ptr<ContextType>)> inner_func = [func](
                const NodeInType& in, NodeOutType& out, std::shared_ptr<ContextType>){
            out =  std::move(func(in));
            return true;
        };
        addNode(inner_func, outResourceInitializer);
    }

    template<typename NodeInType, typename NodeOutType, typename Container = std::vector<NodeOutType>>
    void addNode(std::function<NodeOutType(const NodeInType&, std::shared_ptr<ContextType>)> func,
                 std::function<Container(std::shared_ptr<ContextType>)> outResourceInitializer = nullptr) {
        std::function<NodeOutType(const NodeInType&, std::shared_ptr<ContextType>)> inner_func = [func](
                const NodeInType& in, NodeOutType& out, std::shared_ptr<ContextType> ctx){ out =  std::move(func(in, ctx)); };
        addNode(inner_func, outResourceInitializer);
    }

    template<typename NodeInType, typename NodeOutType, typename Container=std::vector<NodeOutType>>
    void addNode(std::function<bool(const NodeInType&, NodeOutType&)> func,
                 std::function<Container(std::shared_ptr<ContextType>)> outResourceInitializer = nullptr) {
        std::function<bool(const NodeInType&, NodeOutType&, std::shared_ptr<ContextType>)> inner_func = [func](
                const NodeInType& in, NodeOutType& out, std::shared_ptr<ContextType>){ return func(in, out); };
        addNode(inner_func, outResourceInitializer);
    }

    template<typename NodeInType, typename NodeOutType, typename Container=std::vector<NodeOutType>>
    void addNode(std::function<bool(const NodeInType&, NodeOutType&, std::shared_ptr<ContextType>)> func,
                 std::function<Container(std::shared_ptr<ContextType>)> outResourceInitializer = nullptr
                 ) {
       for(size_t i=0; i<pipelines.size(); i++){
           auto& pipeline = pipelines[i];
           if(!pipeline) continue;
           Container outResources;
           try {
               if(outResourceInitializer){
                   outResources = std::move(outResourceInitializer(pipeline->getContext()));
               }
               pipeline->addNode(func, outResources);
           } catch (...) {
               SGLOG(WARNING, "pipeline #%d is not created!", i);
               contextDeinitializer(pipeline->getContext());
               pipeline.reset();
           }
       }
    }

    void start(){
       for(auto& pipeline: pipelines){
           if(pipeline) {
               pipeline->setOutputQueue(outQueue);
               pipeline->start();
           }
       }
    }
    bool allStopped(){
       for(auto& pipeline: pipelines){
           if(!pipeline->isStopped()){
               return false;
           }
       }
       return true;
    }

    bool empty() {
        return outQueue->empty();
    }

    void stop(int index = -1){
        if(index == -1){
            for(auto& pipeline: pipelines){
                if(pipeline) pipeline->stop();
            }
        } else if(index<pipelines.size()){
            pipelines[index]->stop();
        }
    }

    bool canPush(){
        return inQueue->canPush();
    }

    bool push(InType in) {
        if(!allStopped()){
            inQueue->push(in);
            return true;
        }
        return false;
    }

    bool pop(OutType& out) {
        return outQueue->tryPop(out);
    }

    bool waitAndPop(OutType& out) {
        return outQueue->waitAndPop(out);
    }

    void join() {
        inQueue->join();
        for(auto& pipeline: pipelines) {
            pipeline->join();
        }
        outQueue->join();
    }

    ~SGPipelinePool(){
        this->join();
        if(contextDeinitializer){
            for(auto& pipeline: pipelines){
                if(!pipeline) continue;
                contextDeinitializer(pipeline->getContext());
            }
        }

    }
};

}

#endif // SGPIPELINEPOOL_H
