
extern "C"
{

void bmrt_destroy(void* p_bmrt) {}

struct bm_context;
typedef struct bm_context *bm_handle_t;

void bm_dev_free(bm_handle_t handle);

}
