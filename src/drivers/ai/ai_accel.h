/* YamKernel — AI/ML Acceleration Framework v0.3.0
 * Kernel-level compute accelerator abstraction, tensor memory, job dispatch */
#ifndef _DRIVERS_AI_AI_ACCEL_H
#define _DRIVERS_AI_AI_ACCEL_H
#include <nexus/types.h>

/* Tensor data types */
typedef enum {
    DTYPE_F32  = 0,   /* 32-bit float */
    DTYPE_F16  = 1,   /* 16-bit float */
    DTYPE_BF16 = 2,   /* bfloat16 */
    DTYPE_I8   = 3,   /* 8-bit integer (quantized) */
    DTYPE_I32  = 4,
    DTYPE_U8   = 5,
} tensor_dtype_t;

/* Tensor descriptor */
#define TENSOR_MAX_DIMS 8
typedef struct {
    u32              id;
    tensor_dtype_t   dtype;
    u32              ndim;
    u32              shape[TENSOR_MAX_DIMS];
    u32              stride[TENSOR_MAX_DIMS];
    u64              phys_addr;       /* Physical address of data */
    void            *virt_addr;       /* Kernel virtual address */
    usize            size_bytes;      /* Total size in bytes */
    u32              refcount;
    u32              flags;
} tensor_t;

#define TENSOR_FLAG_PINNED     (1 << 0)   /* DMA-pinned */
#define TENSOR_FLAG_CONTIGUOUS (1 << 1)   /* Physically contiguous */
#define TENSOR_FLAG_MAPPED     (1 << 2)   /* Mapped to user space */

/* Compute operations */
typedef enum {
    AI_OP_MATMUL      = 0,
    AI_OP_CONV2D      = 1,
    AI_OP_RELU        = 2,
    AI_OP_SOFTMAX     = 3,
    AI_OP_ADD         = 4,
    AI_OP_MUL         = 5,
    AI_OP_TRANSPOSE   = 6,
    AI_OP_REDUCE_SUM  = 7,
    AI_OP_BATCH_NORM  = 8,
    AI_OP_MAXPOOL     = 9,
    AI_OP_CUSTOM      = 255,
} ai_op_type_t;

/* Compute job */
typedef enum { JOB_PENDING, JOB_RUNNING, JOB_DONE, JOB_ERROR } job_state_t;

typedef struct {
    u32           id;
    ai_op_type_t  op;
    u32           input_ids[4];     /* Input tensor IDs */
    u32           output_id;        /* Output tensor ID */
    u32           num_inputs;
    job_state_t   state;
    u64           submit_tick;
    u64           complete_tick;
    u8            priority;         /* 0=highest */
} ai_job_t;

/* Accelerator device */
typedef struct {
    u32          id;
    char         name[32];
    u32          type;              /* 0=CPU(SIMD), 1=GPU, 2=NPU */
    u64          memory_bytes;      /* Device memory capacity */
    u32          compute_units;     /* Number of ALUs/cores */
    u32          max_batch_size;
    bool         active;
    u64          jobs_completed;
    u64          total_compute_ns;
} ai_device_t;

#define AI_MAX_DEVICES  4
#define AI_MAX_TENSORS  256
#define AI_MAX_JOBS     64

/* Public API */
void ai_accel_init(void);

/* Device management */
u32  ai_device_count(void);
const ai_device_t *ai_device_get(u32 idx);

/* Tensor management */
i32  ai_tensor_alloc(u32 ndim, const u32 *shape, tensor_dtype_t dtype);
void ai_tensor_free(u32 tensor_id);
tensor_t *ai_tensor_get(u32 tensor_id);
void *ai_tensor_data(u32 tensor_id);

/* Job management */
i32  ai_job_submit(ai_op_type_t op, const u32 *inputs, u32 num_in, u32 output, u8 prio);
job_state_t ai_job_status(u32 job_id);
void ai_job_wait(u32 job_id);

/* Statistics */
void ai_print_stats(void);

#endif
