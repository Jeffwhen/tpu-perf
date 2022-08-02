#include "SGDevicePool.h"

namespace bm {

const char* __phaseMap[]={
    "PRE-PROCESS",
    "FOWARD",
    "POST-PROCESS"
};

void *SGDeviceContext::getConfigData() const
{
    return configData;
}

void SGDeviceContext::setConfigData(void *value)
{
    configData = value;
}

SGDeviceContext::SGDeviceContext(DeviceId deviceId, const std::string &bmodel):
    deviceId(deviceId), batchSize(batchSize), configData(nullptr) {
    batchSize = -1;
    SGLOG(INFO, "init context on device %d", deviceId);
    auto status = bm_dev_request(&handle, deviceId);
    BM_ASSERT_EQ(status, BM_SUCCESS);
    pSGRuntime = bmrt_create(handle);
    BM_ASSERT(pSGRuntime != nullptr, "cannot create bmruntime handle");
    net = std::make_shared<SGNetwork>(pSGRuntime, bmodel);
    batchSize = net->getBatchSize();
    net->showInfo();
}

bm_device_mem_t SGDeviceContext::allocDeviceMem(size_t bytes) {
    bm_device_mem_t mem;
    if(bm_malloc_device_byte(handle, &mem, bytes) != BM_SUCCESS){
        SGLOG(FATAL, "cannot alloc device mem, size=%d", bytes);
    }
    mem_to_free.push_back(mem);
    return mem;
}

void SGDeviceContext::freeDeviceMem(bm_device_mem_t &mem){
    auto iter = std::find_if(mem_to_free.begin(), mem_to_free.end(), [&mem](bm_device_mem_t& m){
            return bm_mem_get_device_addr(m) ==  bm_mem_get_device_addr(mem);
    });
    BM_ASSERT(iter != mem_to_free.end(), "cannot free mem!");
    bm_free_device(handle, mem);
    mem_to_free.erase(iter);
}

void SGDeviceContext::allocMemForTensor(TensorPtr tensor){
    auto mem_size = tensor->get_mem_size();
    auto mem = allocDeviceMem(mem_size);
    tensor->set_device_mem(&mem);
}

SGDeviceContext::~SGDeviceContext() {
    auto mems = mem_to_free;
    for(auto m : mems){
        freeDeviceMem(m);
    }
    bmrt_destroy(pSGRuntime);
    bm_dev_free(handle);
}

void ProcessStatInfo::update(const std::shared_ptr<ProcessStatus> &status, size_t batch) {
    if(status->valid){
        numSamples += batch;
        totalDuration += status->totalDuration();
        for(size_t i = durations.size(); i<status->starts.size(); i++){
            durations.push_back(0);
        }
        for(size_t i=0; i<status->starts.size(); i++){
            durations[i] += usBetween(status->starts[i], status->ends[i]);
        }
        deviceProcessNum[status->deviceId] += batch;
    }
}

void ProcessStatInfo::start() {
    startTime=std::chrono::steady_clock::now();
}

uint32_t *ProcessStatInfo::get_durations(unsigned *num) {
    *num = durations.size();
    auto data = new uint32_t[durations.size()];
    std::copy(durations.begin(), durations.end(), data);
    return data;
}

void ProcessStatInfo::show() {
    auto end = std::chrono::steady_clock::now();
    auto totalUs = usBetween(startTime, end);
    SGLOG(INFO, "For model '%s'", name.c_str());
    SGLOG(INFO, "  num_sample=%d: total_time=%gms, avg_time=%gms, speed=%g samples/sec",
          numSamples, totalUs/1000.0, (float)totalUs/1000.0/numSamples,
          numSamples*1e6/totalUs);
    //        SGLOG(INFO, "            serialized_time=%gms, avg_serialized_time=%gms", totalDuration/1000.0, (float)totalDuration/1000.0/numSamples);

    SGLOG(INFO, "Samples process stat:");
    for(auto& p: deviceProcessNum){
        SGLOG(INFO, "  -> device #%d processes %d samples", p.first, p.second);
    }
    SGLOG(INFO, "Average per device:");
    for(size_t i=0; i<durations.size(); i++){
        SGLOG(INFO, "  -> %s total_time=%gms, avg_time=%gms",
              __phaseMap[i],
              durations[i]/1000.0, durations[i]/1000.0/numSamples);
    }
}

void ProcessStatus::reset(){
    starts.clear();
    ends.clear();
    valid = false;
}

void ProcessStatus::start(){
    starts.push_back(std::chrono::steady_clock::now());
    ends.push_back(starts.back());
}

void ProcessStatus::end(){
    ends.back() = std::chrono::steady_clock::now();
}

void ProcessStatus::show() {
    SGLOG(INFO, "device_id=%d, valid=%d, total=%dus", deviceId, valid, totalDuration());
    for(size_t i=0; i<starts.size(); i++){
        auto startStr = steadyToString(starts[i]);
        auto endStr = steadyToString(ends[i]);
        SGLOG(INFO, "  -> %s: duration=%dus",
              __phaseMap[i],
              usBetween(starts[i], ends[i]),
              startStr.c_str(), endStr.c_str());
    }
}

size_t ProcessStatus::totalDuration() const {
    return usBetween(starts.front(), ends.back());
}

}
