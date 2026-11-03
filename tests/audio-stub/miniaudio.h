/* Hardware-free device seam for exercising the production audio.c stop path. */
#include <stdint.h>
typedef uint64_t ma_uint64;
typedef uint32_t ma_uint32;
typedef int ma_result;
typedef int ma_backend;
typedef struct { int unused; } ma_context;
typedef struct { void *pUserData; } ma_device;
typedef struct {
    unsigned sampleRate;
    struct { int format,channels; } playback,capture;
    void (*dataCallback)(ma_device*,void*,const void*,ma_uint32);
    void *pUserData;
} ma_device_config;
#define ma_backend_null 0
#define MA_SUCCESS 0
#define ma_device_type_duplex 1
#define ma_device_type_playback 2
#define ma_format_s16 1
static int device_joined;
static ma_uint64 ma_atomic_load_64(const volatile ma_uint64 *p) { return *p; }
static ma_uint32 ma_atomic_load_32(const volatile ma_uint32 *p) { return *p; }
static void ma_atomic_store_64(volatile ma_uint64 *p,ma_uint64 v) { *p=v; }
static void ma_atomic_store_32(volatile ma_uint32 *p,ma_uint32 v) { *p=v; }
static ma_result ma_context_init(ma_backend *b,int n,void *c,ma_context *o) { (void)b;(void)n;(void)c;(void)o;return 0; }
static void ma_context_uninit(ma_context *c) { (void)c; }
static ma_device_config ma_device_config_init(int type) { ma_device_config c={0};(void)type;return c; }
static ma_result ma_device_init(ma_context *c,const ma_device_config *cfg,ma_device *d) { (void)c; d->pUserData=cfg->pUserData;return 0; }
static ma_result ma_device_start(ma_device *d) { (void)d;device_joined=0;return 0; }
static void ma_device_uninit(ma_device *d) { (void)d;device_joined=1; }
static const char *ma_result_description(ma_result r) { (void)r;return "stub"; }
