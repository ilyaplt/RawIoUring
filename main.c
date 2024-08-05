#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

typedef struct io_sq_s
{
    __u32 *head;
    __u32 *tail;
    __u32 *ring_mask;
    __u32 *array;
    struct io_uring_sqe *sqes;
} io_sq_t;

typedef struct io_cq_s
{
    __u32 *head;
    __u32 *tail;
    __u32 *ring_mask;
    struct io_uring_cqe *cqes;
} io_cq_t;

typedef struct io_ring_s
{
    int fd;
    io_sq_t sq;
    io_cq_t cq;
} io_ring_t;

int io_ring_init(int queue, io_ring_t *ring)
{
    memset(ring, 0, sizeof(io_ring_t));
    struct io_uring_params params = {0};
    int ring_fd = syscall(__NR_io_uring_setup, queue, &params);

    if (ring_fd <= 0) {
        return -1;
    }

    ring->fd = ring_fd;

    io_sq_t *sq = &ring->sq;
    io_cq_t *cq = &ring->cq;

    const size_t sq_sz = params.sq_off.array + params.sq_entries * sizeof(unsigned);

    void *sq_ptr = mmap(NULL,
                        sq_sz,
                        PROT_READ | PROT_WRITE,
                        MAP_POPULATE | MAP_SHARED,
                        ring_fd,
                        IORING_OFF_SQ_RING);

    if (sq_ptr == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    void *sqe_ptr = mmap(NULL,
                         params.sq_entries * sizeof(struct io_uring_sqe),
                         PROT_READ | PROT_WRITE,
                         MAP_POPULATE | MAP_SHARED,
                        ring_fd,
                    IORING_OFF_SQES);

    if (sqe_ptr == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    const size_t cq_sz = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);

    void *cq_ptr = mmap(NULL,
                        cq_sz,
                        PROT_READ | PROT_WRITE,
                        MAP_POPULATE | MAP_SHARED,
                        ring_fd,
                        IORING_OFF_CQ_RING);

    if (cq_ptr == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    sq->array = sq_ptr + params.sq_off.array;
    sq->head = sq_ptr + params.sq_off.head;
    sq->tail = sq_ptr + params.sq_off.tail;
    sq->ring_mask = sq_ptr + params.sq_off.ring_mask;
    sq->sqes = sqe_ptr;

    cq->head = cq_ptr + params.cq_off.head;
    cq->tail = cq_ptr + params.cq_off.tail;
    cq->cqes = cq_ptr + params.cq_off.cqes;
    cq->ring_mask = cq_ptr + params.cq_off.ring_mask;

    return 0;
}

struct io_uring_sqe* io_ring_get_sqe(io_ring_t* ring)
{
    io_sq_t *sq = &ring->sq;

    __u32 tail = *sq->tail;

    __u32 index = tail & *sq->ring_mask;

    sq->array[index] = index;

    *sq->tail += 1;

    return &sq->sqes[index];
}

struct io_uring_cqe* io_ring_get_cqe(io_ring_t *ring) {
    io_cq_t* cq = &ring->cq;

    __u32 head = *cq->head;
    __u32 tail = *cq->tail;

    if (head == tail) return NULL;

    (*cq->head)++;

    __u32 index = head & *cq->ring_mask;

    return &cq->cqes[index];
}

int io_ring_enter(io_ring_t *ring, int to_submit, int min_complete, unsigned int flags)
{
    return (int) syscall(__NR_io_uring_enter, ring->fd, to_submit, min_complete, flags, 0, 0);
}

void io_ring_prep_write(io_ring_t *ring, int fd, const void *buffer, __u32 length)
{
    struct io_uring_sqe *sqe = io_ring_get_sqe(ring);

    sqe->fd = fd;
    sqe->flags = 0;
    sqe->addr = (__u64) buffer;
    sqe->len = length;
    sqe->opcode = IORING_OP_WRITE;
}

int main() {
    io_ring_t ring;

    io_ring_init(1, &ring);

    const char *text = "Hello io_uring!\n";

    io_ring_prep_write(&ring, STDOUT_FILENO, text, strlen(text));

    io_ring_enter(&ring, 1, 1, IORING_ENTER_GETEVENTS);
}
