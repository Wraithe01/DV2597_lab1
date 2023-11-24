/***************************************************************************
 *
 * Parallellized version of Quick sort
 *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <math.h>
#include <sys/time.h>

#define KILO (1024)
#define MEGA (1024*1024)
#define MAX_ITEMS (64*MEGA)
#define swap(v, a, b) {unsigned tmp; tmp=v[a]; v[a]=v[b]; v[b]=tmp;}

#define THREADS 32
#define MINWORKSIZE 256

static int *v;

struct Job {
    int* v;
    unsigned int low;
    unsigned int high;
};
//I have implemented a threadpool that will push and pop jobs of a job stack.
//The job stack is a critical region for the project and uses a mutex for thread safe manipulation
struct Job* JobStack;
unsigned int StackSize;
pthread_mutex_t StackLock = PTHREAD_MUTEX_INITIALIZER;

//Idle threads is a semaphore that keeps track of how many threads are currently idle.
//If there are no idle threads no new jobs may be pushed to the stack, instead the threads will prioritize just doing those jobs as they are already available to them.
//When the final thread finishes its work and it sees all threads are idle, it will prompt all threads to orderly join with the waiting main thread.
sem_t IdleThreads;
//The Recv semaphore is where the idle threads wait for jobs to be available on the job stack.
//Whenever a thread has a job to partition, and there are idle threads available, it will post the Recv semaphore after pushing the job to the stack.
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

//The partition function is unchanged from the sequential version
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

//Recursive function that the thread workers use to sort their sub arrays.
void DoTask(int* v, unsigned int low, unsigned int high) 
{
    unsigned int pivot_index;

    /* no need to sort a vector of zero or one element */
    if (low >= high)
        return;    

    /* select the pivot value */
    pivot_index = (low+high)/2;

    /* partition the vector */
    pivot_index = partition(v, low, high, pivot_index);

    /* sort the two sub arrays */
    //only if both sub arrays are of sufficient size will the thread split the job
    if (((pivot_index-1 - low) >= MINWORKSIZE) && ((high - (pivot_index + 1)) >= MINWORKSIZE)) {
        //Thread checks if there are idle threads to take on the work, otherwise the current thread performs it instead
        if (sem_trywait(&IdleThreads) == 0) {
            //Waits for critical region and pushes job for another thread to complete
            pthread_mutex_lock(&StackLock);
            JobStack[StackSize].v = v;
            JobStack[StackSize].low = low;
            JobStack[StackSize].high = pivot_index - 1;
            StackSize++;
            pthread_mutex_unlock(&StackLock);
            //notify the waiting thread/threads of available jobs
            sem_post(&Recv);

            //The threads always sort the second sub array themselves recursively.
            //No point in pushing this to the job stack just for this very thread to go idle and pick it up again
            //new jobs may still be generated in the next recursive level if there are available threads to take them
            DoTask(v, pivot_index + 1, high);
            return;
        }
    }

    //the thread will start sorting both sub arrays recursively
    //new jobs may still be generated in the next recursive levels if there are available threads to take them
    if (low < pivot_index) {
        DoTask(v, low, pivot_index - 1);
    }
    if (pivot_index < high) {
        DoTask(v, pivot_index + 1, high);
    }
}

void* ThreadWorker(void* arg)
{
    struct Job task;
    int idle;
    while (1) {
        //thread waits until there is work to be done
        sem_wait(&Recv);
        //locks jobstack
        pthread_mutex_lock(&StackLock);
        //if there is no work in the stack the sort has concluded and threads will terminate
        if (StackSize <= 0) {
            //unlocks the critical region of course
            pthread_mutex_unlock(&StackLock);
            //wakes another thread. this way all threads will eventually be woken and exit.
            sem_post(&Recv);
            break;
        }
        //a pending job is consumed
        StackSize--;
        task = JobStack[StackSize];
        //unlocks jobstack
        pthread_mutex_unlock(&StackLock);

        //The job is performed
        DoTask(task.v, task.low, task.high);
        //The thread has recursively worked through all the parts of the array it was assigned and now need to wait for more jobs or to terminate

        //tallies the idle threads
        sem_post(&IdleThreads);

        //if all threads are idle this thread will start the termination of all threads
        sem_getvalue(&IdleThreads, &idle);
        //printf("idle threads: %i\n", idle);
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
    //the thread pool must start with one active thread, or the exit procedure will be activated prematurely.
    //Idle threads is initialized to the number of threads minus the one active thread
    sem_init(&IdleThreads, 0, THREADS - 1);
    //Recv is initialized to one so the first thread that is able too will be able to decrement it and pop the first waiting job on the stack
    sem_init(&Recv, 0, 1);

    pthread_t *threadpool;
    threadpool = malloc(THREADS * sizeof(pthread_t));
    JobStack = malloc(THREADS * sizeof(struct Job));
    StackSize = 0;

    //The initial array is pushed as a job to the jobstack before any thread is created so that once any of them are created and is able to, will take the first jobb.
    JobStack[StackSize].high = high;
    JobStack[StackSize].low = low;
    JobStack[StackSize].v = v;
    StackSize++;

    //Main thread creates all the thread workers
    for (int i = 0; i < THREADS; i++) {
        pthread_create(&(threadpool[i]), NULL, ThreadWorker, NULL);
    }
    //The main thread will remain idle and wait to join all the worker threads
    //So technically the implementation makes use of one more threads than defined, however the main thread performs no useful work and should not have any effects on performance
    for (int i = 0; i < THREADS; i++) {
        pthread_join(threadpool[i], NULL);
    }

    free(threadpool);
    free(JobStack);
}

void Validate()
{
    for (int i = 1; i < MAX_ITEMS; i++)
    {
        if (v[i - 1] > v[i])
        {
            printf("Sort failed\n");
            return;
        }
    }
    printf("Sort Succeeded\n");
}

int
main(int argc, char **argv)
{
    init_array();
    //print_array();
    struct timeval start, end;
    gettimeofday(&start, NULL);
    quick_sort(v, 0, MAX_ITEMS-1);
    gettimeofday(&end, NULL);
    //print_array();

    double timeTaken;
    timeTaken = (end.tv_sec - start.tv_sec) * 1e6;
    timeTaken = (timeTaken + (end.tv_usec - start.tv_usec)) * 1e-6;
    printf("Sort time: %lf sec\n", timeTaken);

    Validate();

    pthread_mutex_destroy(&StackLock);
    sem_destroy(&IdleThreads);
    sem_destroy(&Recv);
}
