#include <bits/types/siginfo_t.h>
#include <bits/types/sigset_t.h>
#include <stdatomic.h>
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
typedef double matrix[MAT_SIZE][MAT_SIZE];


int   N;      /* matrix size		    */
int   maxnum; /* max number of element*/
char* Init;   /* matrix init type	    */
int   PRINT;  /* print switch		    */

matrix mat;         /* matrix A		        */
double b[MAT_SIZE]; /* vector b             */
double y[MAT_SIZE]; /* vector y             */

#define COL_DONE 1
#define COL_WIP 0
uint32_t        colWork[MAT_SIZE];
pthread_mutex_t colLock;
pthread_cond_t  colCond;

pthread_mutex_t workLock;
int32_t         workCounter;

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
    /* timestart = clock(); */
    struct timeval start, end;
    gettimeofday(&start, NULL);
    work();
    gettimeofday(&end, NULL);

    double timeTaken;
    timeTaken = (end.tv_sec - start.tv_sec) * 1e6;
    timeTaken = (timeTaken + (end.tv_usec - start.tv_usec)) * 1e-6;
    printf("Solve time: %lf sec\n", timeTaken);
    /* timeend = clock(); */
    /* printf("Time taken: %8.8gs\n", ((double) (timeend - timestart)) / CLOCKS_PER_SEC); */
    /* for (uint32_t j = 0; j < 5; ++j) */
    /* { */
    /*     for (uint32_t i = 0; i < 5; ++i) */
    /*     { */
    /*         printf("%8.8g\t", mat[j][i]); */
    /*     } */
    /*     printf("\n"); */
    /* } */
    /* printf("===="); */
    /* for (uint32_t i = 0; i < 5; ++i) */
    /* { */
    /*     printf("%8.8g\t", b[i]); */
    /* } */
    /* printf("\n"); */
    if (PRINT == 1)
        Print_Matrix();
    return 0;
}

typedef struct ptargs
{
    uint16_t tid;
} ptargs_s;
void GaussianElimination(uint32_t rowc)
{
    pthread_mutex_lock(&colLock);
    while (rowc != 0 && colWork[rowc - 1] < 1)
        pthread_cond_wait(&colCond, &colLock);
    pthread_mutex_unlock(&colLock);

    // Normalise
    double normzinverse = 1 / mat[rowc][rowc];
    for (uint32_t j = rowc + 1; j < MAT_SIZE; ++j)
        mat[rowc][j] *= normzinverse;
    y[rowc]         = b[rowc] * normzinverse;
    mat[rowc][rowc] = 1.0;

    // Eliminate (rain)
    for (uint32_t i = rowc + 1; i < MAT_SIZE; ++i)
    {
        for (uint32_t j = rowc + 1; j < MAT_SIZE; ++j)
            mat[i][j] -= mat[i][rowc] * mat[rowc][j];
        b[i] -= mat[i][rowc] * y[rowc];
        mat[i][rowc] = 0.0;

        pthread_mutex_lock(&colLock);
        colWork[rowc]++;
        pthread_cond_broadcast(&colCond);
        pthread_mutex_unlock(&colLock);
    }
}
void* ThreadWork(void* input)
{
    ptargs_s args  = *(ptargs_s*) input;
    int32_t  jobid = args.tid;
    while (jobid < MAT_SIZE)
    {
        GaussianElimination(jobid);
        // Get new job
        pthread_mutex_lock(&workLock);
        jobid = workCounter++;
        pthread_mutex_unlock(&workLock);
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

    siginfo_t data;
    sigset_t  sig;
    sigemptyset(&sig);
    sigaddset(&sig, SIGRTMIN);
    // Has to be blocking for sigwait to work
    pthread_sigmask(SIG_BLOCK, &sig, NULL);

    workCounter = THREAD_COUNT;
    memset(colWork, COL_WIP, MAT_SIZE);
    pthread_cond_init(&colCond, NULL);
    pthread_mutex_init(&colLock, NULL);
    pthread_mutex_init(&workLock, NULL);


    // Create thread pool
    for (uint16_t i = 0; i < THREAD_COUNT; ++i)
    {
        targs[i].tid = i;
        pthread_create(&tpool[i], NULL, ThreadWork, &targs[i]);
    }
    // Collect threads
    for (uint16_t i = 0; i < THREAD_COUNT; ++i)
    {
        sigwaitinfo(&sig, &data);
        pthread_join(tpool[data.si_int], NULL);
    }
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
