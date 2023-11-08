/***************************************************************************
 *
 * Parallellized version of Quick sort
 *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <semaphore.h>
#include <math.h>

#define KILO (1024)
#define MEGA (1024*1024)
#define MAX_ITEMS (64*MEGA)
#define swap(v, a, b) {unsigned tmp; tmp=v[a]; v[a]=v[b]; v[b]=tmp;}

#define THREADS 8
#define MINWORKSIZE 128

static int *v;

struct Job {
    int* v;
    unsigned int low;
    unsigned int high;
};

struct Job* JobStack;
unsigned int StackSize;
pthread_mutex_t StackLock = PTHREAD_MUTEX_INITIALIZER;

sem_t IdleThreads;
sem_t Recv;

static void
print_array(void)
{
    int i;
    for (i = 0; i < MAX_ITEMS; i++)
        printf("%d ", v[i]);
    printf("\n");
}

static void
init_array(void)
{
    int i;
    v = (int *) malloc(MAX_ITEMS*sizeof(int));
    for (i = 0; i < MAX_ITEMS; i++)
        v[i] = rand();
}

static unsigned
partition(int *v, unsigned low, unsigned high, unsigned pivot_index)
{
    /* move pivot to the bottom of the vector */
    if (pivot_index != low)
        swap(v, low, pivot_index);

    pivot_index = low;
    low++;

    /* invariant:
     * v[i] for i less than low are less than or equal to pivot
     * v[i] for i greater than high are greater than pivot
     */

    /* move elements into place */
    while (low <= high) {
        if (v[low] <= v[pivot_index])
            low++;
        else if (v[high] > v[pivot_index])
            high--;
        else
            swap(v, low, high);
    }

    /* put pivot back between two groups */
    if (high != pivot_index)
        swap(v, pivot_index, high);
    return high;
}

int DoTask(int* v, unsigned int low, unsigned int high) 
{
    unsigned int pivot_index;

    /* no need to sort a vector of zero or one element */
    if (low == high)
        return 1;
    if (low > high)
        return 0;    

    /* select the pivot value */
    pivot_index = (low+high)/2;

    /* partition the vector */
    pivot_index = partition(v, low, high, pivot_index);

    /* sort the two sub arrays */
    //only if both sub arrays are of sufficient size will the thread split the job
    if ((sem_trywait(&IdleThreads) == 0) && ((pivot_index-1 - low) >= MINWORKSIZE) && ((high - (pivot_index + 1)) >= MINWORKSIZE)) {
        pthread_mutex_lock(&StackLock);
        JobStack[StackSize].v = v;
        JobStack[StackSize].low = low;
        JobStack[StackSize].high = pivot_index - 1;
        StackSize++;
        pthread_mutex_unlock(&StackLock);
        sem_post(&Recv);

        return DoTask(v, pivot_index + 1, high) + 1;
    }

    int localProgress = 0;
    if (low < pivot_index) {
        localProgress += DoTask(v, low, pivot_index - 1);
    }
    if (pivot_index < high) {
        localProgress += DoTask(v, pivot_index + 1, high);
    }
    return localProgress + 1;
}

void* ThreadWorker(void* arg)
{
    unsigned int localProgress = 0;
    struct Job task;
    int idle;
    while (1) {
        //thread waits until there is work to be done
        sem_wait(&Recv);

        //locks jobstack
        pthread_mutex_lock(&StackLock);
        //if there is no work in the stack the sort has concluded and threads will terminate
        if (StackSize <= 0) {
            pthread_mutex_unlock(&StackLock);
            sem_post(&Recv);
            break;
        }
        //a pending job is consumed
        StackSize--;
        task = JobStack[StackSize];
        //unlocks jobstack
        pthread_mutex_unlock(&StackLock);

        //The job is performed
        localProgress = DoTask(task.v, task.low, task.high);

        //tallies the idle threads
        sem_post(&IdleThreads);

        //if all threads are idle this thread will start the termination of all threads
        sem_getvalue(&IdleThreads, &idle);
        if (idle >= THREADS)
        {
            //by posting recv a waiting thread will become active, see the stack is empty, post recv for the next thread and then exits.
            sem_post(&Recv);
            break;
        }
    }
    pthread_exit(NULL);
}

static void
quick_sort(int *v, unsigned low, unsigned high)
{
    sem_init(&IdleThreads, 0, THREADS - 1);
    sem_init(&Recv, 0, 1);
    pthread_t *threadpool;
    threadpool = malloc(THREADS * sizeof(pthread_t));
    JobStack = malloc(THREADS * sizeof(struct Job));
    StackSize = 0;

    JobStack[StackSize].high = high;
    JobStack[StackSize].low = low;
    JobStack[StackSize].v = v;
    StackSize++;

    for (int i = 0; i < THREADS; i++) {
        pthread_create(&(threadpool[i]), NULL, ThreadWorker, NULL);
    }

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threadpool[i], NULL);
    }

    free(threadpool);
    free(JobStack);
}

int
main(int argc, char **argv)
{
    init_array();
    //print_array();
    quick_sort(v, 0, MAX_ITEMS-1);
    //print_array();

    pthread_mutex_destroy(&StackLock);
    sem_destroy(&IdleThreads);
    sem_destroy(&Recv);
}
