#include <string.h>
#include <ring_buffer.h>
#include <assert.h>
#include <kernel_symbol.h>

void ring_buffer_writer_init(struct ring_buffer_writer *writer, void *mem, unsigned int mem_szie)
{
    assert(mem_szie);

    writer->mem = mem;
    writer->buffer_size = mem_szie;
    writer->index = 0;
    writer->n = 0;
}
EXPORT_SYMBOL(ring_buffer_writer_init);

void ring_buffer_reader_init(struct ring_buffer_reader *reader, struct ring_buffer_writer *writer)
{
    reader->writer = writer;
    reader->n = 0;
}
EXPORT_SYMBOL(ring_buffer_reader_init);

unsigned int ring_buffer_used_size(struct ring_buffer_reader *reader)
{
    struct ring_buffer_writer *writer = reader->writer;
    unsigned int delta = writer->n - reader->n;

    if (delta > writer->buffer_size)
        return writer->buffer_size;
    else
        return delta;
}
EXPORT_SYMBOL(ring_buffer_used_size);

unsigned int ring_buffer_free_size(struct ring_buffer_reader *reader)
{
    return reader->writer->buffer_size - ring_buffer_used_size(reader);
}
EXPORT_SYMBOL(ring_buffer_free_size);

unsigned int ring_buffer_capacity(struct ring_buffer_reader *reader)
{
    return reader->writer->buffer_size;
}
EXPORT_SYMBOL(ring_buffer_capacity);

unsigned int ring_buffer_capacity2(struct ring_buffer_writer *writer)
{
    return writer->buffer_size;
}
EXPORT_SYMBOL(ring_buffer_capacity2);

unsigned int ring_buffer_get_writer_index(struct ring_buffer_writer *writer)
{
    return writer->index;
}
EXPORT_SYMBOL(ring_buffer_get_writer_index);

unsigned int ring_buffer_get_reader_index(struct ring_buffer_reader *reader)
{
    struct ring_buffer_writer *writer = reader->writer;
    unsigned int delta;
    unsigned int reader_index;

    delta = writer->n - reader->n;
    if (delta > writer->buffer_size) {
        reader->n += delta - writer->buffer_size;
        delta = writer->buffer_size;
    }

    reader_index = writer->index + writer->buffer_size - delta;
    reader_index %= writer->buffer_size;

    return reader_index;
}
EXPORT_SYMBOL(ring_buffer_get_reader_index);

void ring_buffer_add_writer(struct ring_buffer_writer *writer, unsigned int n)
{
    writer->n += n;

    if (n <= writer->buffer_size)
        writer->index += n;
    else
        writer->index += n % writer->buffer_size;

    writer->index %= writer->buffer_size;
}
EXPORT_SYMBOL(ring_buffer_add_writer);

void ring_buffer_write(struct ring_buffer_writer *writer, void *src, unsigned int n)
{
    if (n > writer->buffer_size) {
        ring_buffer_add_writer(writer, n - writer->buffer_size);
        src += n - writer->buffer_size;
        n = writer->buffer_size;
    }

    if (writer->index + n <= writer->buffer_size) {
        memcpy(writer->mem + writer->index, src, n);
    } else {
        unsigned int tmp = writer->buffer_size - writer->index;
        memcpy(writer->mem + writer->index, src, tmp);
        memcpy(writer->mem, src + tmp, n - tmp);
    }

    ring_buffer_add_writer(writer, n);
}
EXPORT_SYMBOL(ring_buffer_write);

unsigned int ring_buffer_add_reader(struct ring_buffer_reader *reader, unsigned int n)
{
    struct ring_buffer_writer *writer = reader->writer;
    unsigned int delta;

    delta = writer->n - reader->n;
    if (delta > writer->buffer_size) {
        reader->n += delta - writer->buffer_size;
        delta = writer->buffer_size;
    }

    if (n > delta)
        n = delta;

    reader->n += n;

    return n;
}
EXPORT_SYMBOL(ring_buffer_add_reader);

unsigned int ring_buffer_read(struct ring_buffer_reader *reader, void *dst, unsigned int n)
{
    struct ring_buffer_writer *writer = reader->writer;
    unsigned int delta;
    unsigned int reader_index;

    delta = writer->n - reader->n;
    if (delta > writer->buffer_size) {
        reader->n += delta - writer->buffer_size;
        delta = writer->buffer_size;
    }

    if (n > delta)
        n = delta;

    reader->n += n;

    reader_index = writer->index + writer->buffer_size - delta;
    reader_index %= writer->buffer_size;

    if (reader_index + n <= writer->buffer_size) {
        memcpy(dst, writer->mem + reader_index, n);
    } else {
        unsigned int tmp = writer->buffer_size - reader_index;
        memcpy(dst, writer->mem + reader_index, tmp);
        memcpy(dst + tmp, writer->mem, n - tmp);
    }

    return n;
}
EXPORT_SYMBOL(ring_buffer_read);
