#include <string.h>

#include "ring_mem.h"

void ring_mem_init(struct ring_mem *ring, void *mem, int size)
{
    ring->mem_addr = (unsigned long)mem;
    ring->mem_size = size;
    ring->write_pos = 0;
    ring->read_pos = 0;
    ring->write_addr = mem;
    ring->read_addr = mem;
}

int ring_mem_readable_size(struct ring_mem *ring)
{
    return ring->write_pos - ring->read_pos;
}

int ring_mem_writable_size(struct ring_mem *ring)
{
    return ring->mem_size - ring_mem_readable_size(ring);
}

int ring_mem_write(struct ring_mem *ring, const void *buf, unsigned int size)
{
    void *mem = (void *)ring->mem_addr;
    int N = ring_mem_writable_size(ring);
    int pos = ring->write_pos % ring->mem_size;

    if (!N)
        return 0;

    if (size > N)
        size = N;

    if (buf) {
        if (pos + size > ring->mem_size) {
            int sz = ring->mem_size - pos;
            memcpy(mem + pos, buf, sz);
            memcpy(mem, buf + sz, size - sz);
        } else {
            memcpy(mem + pos, buf, size);
        }
    }

    ring->write_pos += size;

    return size;
}

int ring_mem_read(struct ring_mem *ring, void *buf, unsigned int size)
{
    void *mem = (void *)ring->read_addr;

    int N = ring_mem_readable_size(ring);
    int pos = ring->read_pos % ring->mem_size;

    if (!N)
        return 0;

    if (size > N)
        size = N;

    if (buf) {
        if (pos + size > ring->mem_size) {
            int sz = ring->mem_size - pos;
            memcpy(buf, mem + pos, sz);
            memcpy(buf + sz, mem, size - sz);
        } else {
            memcpy(buf, mem + pos, size);
        }
    }

    ring->read_pos += size;

    return size;
}

void ring_mem_clean(struct ring_mem *ring)
{
    ring->read_pos = ring->write_pos;
}
