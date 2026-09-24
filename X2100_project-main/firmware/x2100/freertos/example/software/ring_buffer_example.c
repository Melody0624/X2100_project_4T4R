
#include <os.h>
#include <common.h>

static struct ring_buffer_writer writer;
static struct ring_buffer_reader reader;
static char buf[10000];

thread_ptr_t thread1, thread2;

void buffer_writer_thread(void *data)
{
    unsigned char a = 0;
    char buf[99];

    while (1) {
        printf("@");

        for (int i = 0; i < sizeof(buf); i++) {
            buf[i] = a++;
        }
        while (ring_buffer_free_size(&reader) >= sizeof(buf));

        os_enter_critical();

        ring_buffer_write(&writer, buf, sizeof(buf));

        os_exit_critical();
    }
}


void buffer_reader_thread(void *data)
{
    unsigned char a = 0;
    unsigned char buf[1000];

    while (1) {
        int n = sizeof(buf);

        os_enter_critical();
        n = ring_buffer_read(&reader, buf, n);
        os_exit_critical();

        if (n)
            printf("!");

        for (int i = 0; i < n; i++) {
            unsigned char tmp = a++;
            if (buf[i] != tmp)
                panic("not equal: %x %x\n", (int)buf[i], (int)tmp);
        }
    }
}

void ring_buffer_test2(void)
{
    ring_buffer_writer_init(&writer, buf, sizeof(buf));
    ring_buffer_reader_init(&reader, &writer);
    thread1 = thread_create("writer", 8192, buffer_writer_thread, NULL);
    thread2 = thread_create("reader", 8192, buffer_reader_thread, NULL);
}

#include <stdio.h>
#include <string.h>
#include <ring_buffer.h>
void ring_buffer_test(void)
{
    struct ring_buffer_writer writer;
    struct ring_buffer_reader reader0;
    struct ring_buffer_reader reader1;

    char buf[150];
    memset(buf, 0, sizeof(buf));
    ring_buffer_writer_init(&writer, buf, sizeof(buf));
    ring_buffer_reader_init(&reader0, &writer);
    ring_buffer_reader_init(&reader1, &writer);

    char mem[100];
    memset(mem, 'A', sizeof(mem));
    ring_buffer_write(&writer, mem, sizeof(mem));

    char mem0[200];
    memset(mem0, 0, sizeof(mem0));
    unsigned int ret0 = ring_buffer_read(&reader0, mem0, sizeof(mem0));

    char mem1[200];
    memset(mem1, 0, sizeof(mem1));
    unsigned int ret1 = ring_buffer_read(&reader1, mem1, 50);

    printf("ret0: %d  ret1: %d\n", ret0, ret1);
    printf("mem0: %s\n", mem0);
    printf("mem1: %s\n", mem1);

    printf("size0: %d %d\n", ring_buffer_used_size(&reader0), ring_buffer_free_size(&reader0));
    printf("size1: %d %d\n", ring_buffer_used_size(&reader1), ring_buffer_free_size(&reader1));

}
