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
//#define MAX_ITEMS (64*MEGA)
#define MAX_ITEMS (64*MEGA)
#define swap(v, a, b) {unsigned tmp; tmp=v[a]; v[a]=v[b]; v[b]=tmp;}

#define THREADS 4
#define MINWORKSIZE 16

static int *v;

struct Job {
    int* v;
    unsigned int low;
    unsigned int high;
};

struct Job* JobStack;
unsigned int StackSize;
unsigned int StackSpace;
unsigned int PeakStack;
pthread_mutex_t StackLock = PTHREAD_MUTEX_INITIALIZER;
unsigned int Progress;
unsigned int TotalWork;
pthread_mutex_t ProgressLock = PTHREAD_MUTEX_INITIALIZER;
sem_t WaitingThreads ;

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

void AddJob(int* v, unsigned int low, unsigned int high)
{
    pthread_mutex_lock(&StackLock);
    //reallocate jobstack if needed
    if ((StackSize + 1) > StackSpace) {
        StackSpace = ceil(((float)StackSpace) * 1.5f);
        JobStack = realloc(JobStack, StackSpace * sizeof(struct Job));
        fprintf(stderr, "Jobstack increased to %i\n", StackSpace);
    }
    JobStack[StackSize].v = v;
    JobStack[StackSize].low = low;
    JobStack[StackSize].high = high;
    StackSize++;
    pthread_mutex_unlock(&StackLock);
    sem_post(&WaitingThreads);
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
    if (((pivot_index-1 - low) >= MINWORKSIZE) && ((high - (pivot_index + 1)) >= MINWORKSIZE)) {
        AddJob(v, low, pivot_index - 1);

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
    while (1) {
        //thread waits until there is work to be done
        sem_wait(&WaitingThreads);

        //locks jobstack
        pthread_mutex_lock(&StackLock);
        //if there is no work in the stack the sort has concluded and threads will terminate
        if (StackSize <= 0) {
            pthread_mutex_unlock(&StackLock);
            break;
        }
        if (StackSize > PeakStack)
            PeakStack = StackSize;
        //a pending job is consumed
        StackSize--;
        task = JobStack[StackSize];
        //unlocks jobstack
        pthread_mutex_unlock(&StackLock);

        //The job is performed
        localProgress = DoTask(task.v, task.low, task.high);

        //locks progress tally
        pthread_mutex_lock(&ProgressLock);
        //updates the number of items in correct positions
        Progress += localProgress;
        //if the work is complete this thread will wake the other threads so they may terminate
        if (Progress >= TotalWork) {
            if (StackSize != 0)
            {
                fprintf(stderr, "ERROR:Termination error remaining work %i\n", StackSize);
            }
            if (Progress > TotalWork)
            {
                fprintf(stderr, "ERROR:Termination error progress tally error. progress %i out of total %i\n", Progress, TotalWork);
            }
            for (int i = 0; i < THREADS - 1; i++) {
                sem_post(&WaitingThreads);
            }
            pthread_mutex_unlock(&ProgressLock);
            break;
        }
        pthread_mutex_unlock(&ProgressLock);
    }
    pthread_exit(NULL);
}

static void
quick_sort(int *v, unsigned low, unsigned high)
{
    sem_init(&WaitingThreads, 0, 0);
    pthread_t *threadpool;
    threadpool = malloc(THREADS * sizeof(pthread_t));
    StackSpace = pow((int)log2(MAX_ITEMS), 1) * THREADS;
    printf("start space %i\n", StackSpace);
    JobStack = malloc(StackSpace * sizeof(struct Job));
    StackSize = 0;
    PeakStack = 0;
    Progress = 0;
    TotalWork = high + 1 - low;

    for (int i = 0; i < THREADS; i++) {
        pthread_create(&(threadpool[i]), NULL, ThreadWorker, NULL);
    }

    JobStack[StackSize].high = high;
    JobStack[StackSize].low = low;
    JobStack[StackSize].v = v;
    StackSize++;
    sem_post(&WaitingThreads);

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threadpool[i], NULL);
    }
    printf("used stack %i\n", PeakStack);
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
    pthread_mutex_destroy(&ProgressLock);
    sem_destroy(&WaitingThreads);
}
