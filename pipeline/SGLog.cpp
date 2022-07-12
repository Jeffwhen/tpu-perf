#include "SGLog.h"
#include "SGEnv.h"

namespace bm {
static LogLevel SGRT_LOG_LEVEL_THRESHOLD;

int get_log_level()
{
    return SGRT_LOG_LEVEL_THRESHOLD;
}

void set_log_level(LogLevel level){
    SGRT_LOG_LEVEL_THRESHOLD = level;
}

struct __log_initializer{
    __log_initializer(){
        auto level = LogLevel::INFO;
        set_log_level(level);
        set_env_log_level();
    }
};
static __log_initializer __log_init();

void set_env_log_level(LogLevel level)
{
    auto level_cstr = getenv(BM_LOG_LEVEL);
    if(level_cstr){
        level = (LogLevel)atoi(level_cstr);
    }
    set_log_level(level);
}

}
