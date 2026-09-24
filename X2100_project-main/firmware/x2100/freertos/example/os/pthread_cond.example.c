#include <pthread.h>
#include <stdio.h>

static int buf = 0;
static pthread_mutex_t mutex;
static pthread_mutexattr_t mutexattr;
static pthread_cond_t cond;


static void *producer(void *arg)
{
    int i;
    for (i = 1; i < 3; i++)
    {
        pthread_mutex_lock(&mutex);

        buf = i;
        printf("producer produces %d\n", buf);

        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

static void *consumer(void *arg)
{
    int i;
    for (i = 1; i < 3; i++)
    {
        pthread_mutex_lock(&mutex);

        while (buf == 0)
        {
            pthread_cond_wait(&cond, &mutex);
        }

        printf("consumer consumes %d\n", buf);
        buf = 0;

        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

void test_cond(void)
{
    pthread_t pro, con;

    pthread_mutex_init(&mutex, &mutexattr);
    pthread_cond_init(&cond, NULL);

    pthread_create(&pro, NULL, producer, NULL);
    pthread_create(&con, NULL, consumer, NULL);

    pthread_join(pro, NULL);
    pthread_join(con, NULL);

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
}