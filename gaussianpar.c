#include <bits/types/siginfo_t.h>
#include <bits/types/sigset_t.h>
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <bits/pthreadtypes.h>
#include <signal.h>
#include <threads.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <sys/time.h>

#define MAT_SIZE 2048
#define THREAD_COUNT 16
#define LINES_PER_THREAD 8
typedef double matrix[MAT_SIZE][MAT_SIZE];


int   N;      /* matrix size		    */
int   maxnum; /* max number of element*/
char* Init;   /* matrix init type	    */
int   PRINT;  /* print switch		    */

matrix mat;         /* matrix A		        */
double b[MAT_SIZE]; /* vector b             */
double y[MAT_SIZE]; /* vector y             */

pthread_barrier_t comBarrier;
pthread_barrier_t threadBarrier;
pthread_mutex_t   lock;

uint32_t startLine;
uint32_t currentLine;

void work(void);
void Init_Matrix(void);
void Print_Matrix(void);
void Init_Default(void);
int  Read_Options(int, char**);

int main(int argc, char** argv)
{
    int    i, iter;
    time_t timestart, timeend;

    Init_Default();           /* Init default values	*/
    Read_Options(argc, argv); /* Read arguments	*/
    Init_Matrix();            /* Init the matrix	*/

    struct timeval start, end;
    gettimeofday(&start, NULL);
    work();
    gettimeofday(&end, NULL);

    double timeTaken;
    timeTaken = (end.tv_sec - start.tv_sec) * 1e6;
    timeTaken = (timeTaken + (end.tv_usec - start.tv_usec)) * 1e-6;
    printf("Solve time: %lf sec\n", timeTaken);


    FILE* seqA = fopen("seqA.txt", "r");
    FILE* seqB = fopen("seqB.txt", "r");
    FILE* seqY = fopen("seqY.txt", "r");
    for (unsigned int i = 0; i < MAT_SIZE; ++i)
    {
        char*  buf      = malloc(256);
        size_t buffsize = 256;

        getline(&buf, &buffsize, seqB);
        if (fabs(atof(buf) - b[i]) > 0.001f)
            printf("[b] Error on line %d. Was %f, should be %s\n", i + 1, b[i], buf);

        getline(&buf, &buffsize, seqY);
        if (fabs(atof(buf) - y[i]) > 0.001f)
            printf("[y] Error on line %d. Was %f, should be %s\n", i + 1, y[i], buf);

        for (unsigned int j = 0; j < MAT_SIZE; ++j)
        {
            getline(&buf, &buffsize, seqA);
            if (fabs(atof(buf) - mat[i][j]) > 0.0001f)
                printf("[A] Error on line %d. Was %f, should be %s\n",
                       (j + 1) + i * MAT_SIZE,
                       mat[i][j],
                       buf);
        }
    }
    printf("[+] Finished comparing seq and par.\n");
    fclose(seqA);
    fclose(seqB);
    fclose(seqY);

    if (PRINT == 1)
        Print_Matrix();
    return 0;
}

void Elimination(uint32_t rowc)
{
    uint32_t recentlyNormz = startLine - 1;
    // From seq:
    // i = rowc
    // k = recentlyNormz
    for (uint32_t j = recentlyNormz + 1; j < MAT_SIZE; ++j)
        mat[rowc][j] -= mat[rowc][recentlyNormz] * mat[recentlyNormz][j];
    b[rowc] -= mat[rowc][recentlyNormz] * y[recentlyNormz];
    mat[rowc][recentlyNormz] = 0.0;
}
void Normalize(uint32_t rowc)
{
    double normzinverse = 1 / mat[rowc][rowc];
    for (uint32_t i = rowc + 1; i < MAT_SIZE; ++i)
        mat[rowc][i] *= normzinverse;
    y[rowc]         = b[rowc] * normzinverse;
    mat[rowc][rowc] = 1.0;
}

typedef struct ptargs
{
    uint16_t tid;
} ptargs_s;
void* ThreadWork(void* input)
{
    ptargs_s args = *(ptargs_s*) input;
    uint32_t line = 0;

    pthread_barrier_wait(&comBarrier);
    pthread_barrier_wait(&comBarrier);

    while (currentLine < MAT_SIZE)
    {
        // Let everyone compare before moving on
        pthread_barrier_wait(&threadBarrier);

        // Get init line
        pthread_mutex_lock(&lock);
        line = currentLine;
        currentLine += LINES_PER_THREAD;
        pthread_mutex_unlock(&lock);

        // Work on lines
        while (line < MAT_SIZE)
        {
            uint32_t stop = line + LINES_PER_THREAD;
            for (; line < MAT_SIZE && line < stop; ++line)
                Elimination(line);

            // Get new job
            pthread_mutex_lock(&lock);
            line = currentLine;
            currentLine += LINES_PER_THREAD;
            pthread_mutex_unlock(&lock);
        }
        pthread_barrier_wait(&comBarrier);  // Await all threads
        pthread_barrier_wait(&comBarrier);  // Currentline is updated
    }

    // Signal work is done to main thread
    const union sigval retval = { .sival_int = args.tid };
    sigqueue(getpid(), SIGRTMIN, retval);
    pthread_exit(NULL);
}
void work(void)
{
    // Init
    pthread_t tpool[THREAD_COUNT];
    ptargs_s  targs[THREAD_COUNT];
    startLine   = 0;
    currentLine = 0;

    // Sig init
    siginfo_t data;
    sigset_t  sig;
    sigemptyset(&sig);
    sigaddset(&sig, SIGRTMIN);
    // Has to be blocking for sigwait to work
    pthread_sigmask(SIG_BLOCK, &sig, NULL);

    // Mutex init
    pthread_barrier_init(&comBarrier, NULL, THREAD_COUNT + 1);
    pthread_barrier_init(&threadBarrier, NULL, THREAD_COUNT);
    pthread_mutex_init(&lock, NULL);

    // Create thread pool
    for (uint16_t i = 0; i < THREAD_COUNT; ++i)
    {
        targs[i].tid = i;
        pthread_create(&tpool[i], NULL, ThreadWork, &targs[i]);
    }

    // Normalize until last line is done
    while (startLine < MAT_SIZE)
    {
        pthread_barrier_wait(&comBarrier);  // Work done
        Normalize(startLine);
        currentLine = ++startLine;
        pthread_barrier_wait(&comBarrier);  // Updated currentLine
    }

    // Collect threads
    for (uint16_t i = 0; i < THREAD_COUNT; ++i)
    {
        sigwaitinfo(&sig, &data);
        pthread_join(tpool[data.si_int], NULL);
    }

    // Cleanup
    pthread_barrier_destroy(&comBarrier);
    pthread_barrier_destroy(&threadBarrier);
    pthread_mutex_destroy(&lock);
}

void Init_Matrix()
{
    int i, j;

    printf("\nsize      = %dx%d ", N, N);
    printf("\nmaxnum    = %d \n", maxnum);
    printf("Init	  = %s \n", Init);
    printf("Initializing matrix...");

    if (strcmp(Init, "rand") == 0)
    {
        for (i = 0; i < N; i++)
        {
            for (j = 0; j < N; j++)
            {
                if (i == j) /* diagonal dominance */
                    mat[i][j] = (double) (rand() % maxnum) + 5.0;
                else
                    mat[i][j] = (double) (rand() % maxnum) + 1.0;
            }
        }
    }
    if (strcmp(Init, "fast") == 0)
    {
        for (i = 0; i < N; i++)
        {
            for (j = 0; j < N; j++)
            {
                if (i == j) /* diagonal dominance */
                    mat[i][j] = 5.0;
                else
                    mat[i][j] = 2.0;
            }
        }
    }

    /* Initialize vectors b and y */
    for (i = 0; i < N; i++)
    {
        b[i] = 2.0;
        y[i] = 1.0;
    }

    printf("done \n\n");
    if (PRINT == 1)
        Print_Matrix();
}

void Print_Matrix()
{
    int i, j;

    printf("Matrix A:\n");
    for (i = 0; i < N; i++)
    {
        printf("[");
        for (j = 0; j < N; j++)
            printf(" %5.2f,", mat[i][j]);
        printf("]\n");
    }
    printf("Vector b:\n[");
    for (j = 0; j < N; j++)
        printf(" %5.2f,", b[j]);
    printf("]\n");
    printf("Vector y:\n[");
    for (j = 0; j < N; j++)
        printf(" %5.2f,", y[j]);
    printf("]\n");
    printf("\n\n");
}

void Init_Default()
{
    N      = 2048;
    Init   = "rand";
    maxnum = 15.0;
    PRINT  = 0;
}

int Read_Options(int argc, char** argv)
{
    char* prog;

    prog = *argv;
    while (++argv, --argc > 0)
        if (**argv == '-')
            switch (*++*argv)
            {
                case 'n':
                    --argc;
                    N = atoi(*++argv);
                    break;
                case 'h':
                    printf("\nHELP: try sor -u \n\n");
                    exit(0);
                    break;
                case 'u':
                    printf("\nUsage: gaussian [-n problemsize]\n");
                    printf("           [-D] show default values \n");
                    printf("           [-h] help \n");
                    printf("           [-I init_type] fast/rand \n");
                    printf("           [-m maxnum] max random no \n");
                    printf("           [-P print_switch] 0/1 \n");
                    exit(0);
                    break;
                case 'D':
                    printf("\nDefault:  n         = %d ", N);
                    printf("\n          Init      = rand");
                    printf("\n          maxnum    = 5 ");
                    printf("\n          P         = 0 \n\n");
                    exit(0);
                    break;
                case 'I':
                    --argc;
                    Init = *++argv;
                    break;
                case 'm':
                    --argc;
                    maxnum = atoi(*++argv);
                    break;
                case 'P':
                    --argc;
                    PRINT = atoi(*++argv);
                    break;
                default:
                    printf("%s: ignored option: -%s\n", prog, *argv);
                    printf("HELP: try %s -u \n\n", prog);
                    break;
            }
}
