/* ============================================================================
 * YamKernel — Anonymous Pipe Implementation
 * Ring-buffer based unidirectional byte stream.
 * Integrates with VFS file_operations for read/write via FDs.
 * ============================================================================ */
#include "pipe.h"
#include "../fs/vfs.h"
#include "../mem/heap.h"
#include "../lib/string.h"
#include "../lib/kprintf.h"
#include "../sched/sched.h"

/* ---- Pipe File Operations ---- */

static isize pipe_read(file_t *file, void *buf, usize count) {
    pipe_t *p = (pipe_t *)file->private_data;
    if (!p) return -1;

    u64 flags = spin_lock_irqsave(&p->lock);

    /* Block until data is available or all writers have closed */
    while (p->count == 0) {
        if (p->writers_open == 0) {
            spin_unlock_irqrestore(&p->lock, flags);
            return 0;  /* EOF */
        }
        spin_unlock_irqrestore(&p->lock, flags);
        wq_sleep(&p->readers);
        flags = spin_lock_irqsave(&p->lock);
    }

    /* Read as much as available */
    usize to_read = count < p->count ? count : p->count;
    u8 *dst = (u8 *)buf;
    for (usize i = 0; i < to_read; i++) {
        dst[i] = p->buf[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count -= to_read;

    spin_unlock_irqrestore(&p->lock, flags);

    /* Wake writers that may have been blocked on a full buffer */
    wq_wake_one(&p->writers);
    return (isize)to_read;
}

static isize pipe_write(file_t *file, const void *buf, usize count) {
    pipe_t *p = (pipe_t *)file->private_data;
    if (!p) return -1;

    u64 flags = spin_lock_irqsave(&p->lock);

    /* Block until space is available */
    while (p->count >= PIPE_BUF_SIZE) {
        if (p->readers_open == 0) {
            spin_unlock_irqrestore(&p->lock, flags);
            return -1;  /* Broken pipe */
        }
        spin_unlock_irqrestore(&p->lock, flags);
        wq_sleep(&p->writers);
        flags = spin_lock_irqsave(&p->lock);
    }

    usize space = PIPE_BUF_SIZE - p->count;
    usize to_write = count < space ? count : space;
    const u8 *src = (const u8 *)buf;
    for (usize i = 0; i < to_write; i++) {
        p->buf[p->write_pos] = src[i];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count += to_write;

    spin_unlock_irqrestore(&p->lock, flags);

    /* Wake readers that may have been blocking */
    wq_wake_one(&p->readers);
    return (isize)to_write;
}

static int pipe_poll_read(file_t *file) {
    pipe_t *p = (pipe_t *)file->private_data;
    if (!p) return 0;
    return (p->count > 0 || p->writers_open == 0) ? 1 : 0;
}

static int pipe_poll_write(file_t *file) {
    pipe_t *p = (pipe_t *)file->private_data;
    if (!p) return 0;
    return (p->count < PIPE_BUF_SIZE || p->readers_open == 0) ? 1 : 0;
}

static int pipe_close_read(file_t *file) {
    pipe_t *p = (pipe_t *)file->private_data;
    if (p) {
        p->readers_open--;
        wq_wake_all(&p->writers);
        if (p->readers_open == 0 && p->writers_open == 0) {
            kfree(p);
        }
    }
    return 0;
}

static int pipe_close_write(file_t *file) {
    pipe_t *p = (pipe_t *)file->private_data;
    if (p) {
        p->writers_open--;
        wq_wake_all(&p->readers);
        if (p->readers_open == 0 && p->writers_open == 0) {
            kfree(p);
        }
    }
    return 0;
}

static file_operations_t pipe_read_fops = {
    .read  = pipe_read,
    .write = NULL,
    .mmap  = NULL,
    .poll  = pipe_poll_read,
    .close = pipe_close_read,
};

static file_operations_t pipe_write_fops = {
    .read  = NULL,
    .write = pipe_write,
    .mmap  = NULL,
    .poll  = pipe_poll_write,
    .close = pipe_close_write,
};

/* ---- Public API ---- */
int sys_pipe(int fds[2]) {
    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!p) return -1;
    memset(p, 0, sizeof(*p));
    p->lock = (spinlock_t)SPINLOCK_INIT;
    wq_init(&p->readers);
    wq_init(&p->writers);
    p->readers_open = 1;
    p->writers_open = 1;

    /* Create read-end file */
    file_t *rf = (file_t *)kmalloc(sizeof(file_t));
    if (!rf) { kfree(p); return -1; }
    memset(rf, 0, sizeof(*rf));
    rf->fops = &pipe_read_fops;
    rf->private_data = p;
    rf->ref_count = 1;

    /* Create write-end file */
    file_t *wf = (file_t *)kmalloc(sizeof(file_t));
    if (!wf) { kfree(rf); kfree(p); return -1; }
    memset(wf, 0, sizeof(*wf));
    wf->fops = &pipe_write_fops;
    wf->private_data = p;
    wf->ref_count = 1;

    int rfd = fd_alloc(rf);
    int wfd = fd_alloc(wf);
    if (rfd < 0 || wfd < 0) {
        if (rfd >= 0) fd_free(rfd);
        if (wfd >= 0) fd_free(wfd);
        kfree(rf); kfree(wf); kfree(p);
        return -1;
    }

    fds[0] = rfd;
    fds[1] = wfd;
    kprintf_color(0xFF00FF88, "[PIPE] Created: read=fd%d, write=fd%d\n", rfd, wfd);
    return 0;
}
