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

#define MAT_SIZE 4096
#define THREAD_COUNT 16
// Amount of bytes to work per row x column for each checkerboard slot which a single thread will
// work with. This must be of the format of 2^x
#define WORKLOAD 64


typedef double matrix[MAT_SIZE][MAT_SIZE];


int   N;      /* matrix size		    */
int   maxnum; /* max number of element*/
char* Init;   /* matrix init type	    */
int   PRINT;  /* print switch		    */

matrix mat;         /* matrix A		        */
double b[MAT_SIZE]; /* vector b             */
double y[MAT_SIZE]; /* vector y             */
double normzValues[MAT_SIZE];
matrix elimValues;

int32_t         normzLine;
pthread_cond_t  lineCond;
pthread_mutex_t lineLock;

pthread_mutex_t linewLock;
pthread_cond_t  linewCond;
int32_t         lineWork[MAT_SIZE / WORKLOAD];

pthread_mutex_t colLock;
pthread_cond_t  colCond;
int32_t         colWork[MAT_SIZE / WORKLOAD];

void work(void);
void Init_Matrix(void);
void Print_Matrix(void);
void Init_Default(void);
int  Read_Options(int, char**);

int main(int argc, char** argv)
{
    int i, timestart, timeend, iter;

    Init_Default();           /* Init default values	*/
    Read_Options(argc, argv); /* Read arguments	*/
    Init_Matrix();            /* Init the matrix	*/
    time_t duration = clock();
    work();
    printf("Time taken: %8.8g\n", ((double)(clock() - duration))/CLOCKS_PER_SEC);
    if (PRINT == 1)
        Print_Matrix();
    return 0;
}


typedef struct ptargs
{
    uint32_t tid;
    uint32_t startRow;
    uint32_t startCol;
} ptargs_s;
void* HybridBlock(void* input)
{
    ptargs_s* args    = (ptargs_s*) input;
    uint32_t  maxrows = args->startRow + WORKLOAD;
    uint32_t  maxcols = args->startCol + WORKLOAD;


    // Wait until all (left) elimitations are done
    pthread_mutex_lock(&colLock);
    while (colWork[(int) (args->startRow / WORKLOAD)] > 0)
        pthread_cond_wait(&colCond, &colLock);
    pthread_mutex_unlock(&colLock);


    // Eliminate before cross section
    for (uint32_t row = args->startRow; row < maxrows; ++row)
    {
        for (uint32_t col = args->startCol; col < row; ++col)
        {
            double val  = mat[row][col];
            double yval = 0;
            for (uint32_t i = 0; i < row; ++i)
            {
                val += mat[i][col] * elimValues[row][i];
                yval += y[i] * elimValues[row][i];
            }
            elimValues[row][col] = val;
            mat[row][col]        = 0;
            y[row] += yval;
        }

        // We have reached the cross section
        normzValues[row] = 1 / mat[row][row];
        mat[row][row]    = 1;

        pthread_mutex_lock(&lineLock);
        ++normzLine;
        pthread_cond_broadcast(&lineCond);
        pthread_mutex_unlock(&lineLock);
    }
    // for (uint32_t row = args->startRow; row < maxrows; ++row)
    // {
    //     for (uint32_t col = row; col < maxcols; ++col)
    //     {
    //         // Normalise remaining values
    //         double inversediv = normzValues[row];
    //         for (; col < maxcols; ++col)
    //         {
    //             double finalval = mat[row][col];
    //             for (uint32_t i = 0; i < row; ++i)
    //                 finalval += elimValues[row][i] * mat[i][col];
    //             mat[row][col] = finalval * inversediv;
    //         }
    //         y[row] *= inversediv;
    //     }
    // }

    // Column block is done with work
    pthread_mutex_lock(&linewLock);
    ++(lineWork[(int) (args->startCol / WORKLOAD)]);
    pthread_cond_broadcast(&linewCond);
    pthread_mutex_unlock(&linewLock);

    // Signal work is done to main thread
    const union sigval retval = { .sival_int = args->tid };
    sigqueue(getpid(), SIGRTMIN, retval);
    pthread_exit(0);
}
void* EliminationBlock(void* input)
{
    ptargs_s* args    = (ptargs_s*) input;
    uint32_t  maxrows = args->startRow + WORKLOAD;
    uint32_t  maxcols = args->startCol + WORKLOAD;

    // Check if data is available
    pthread_mutex_lock(&linewLock);
    while (lineWork[(int) (args->startCol / WORKLOAD)] < (args->startCol / WORKLOAD))
        pthread_cond_wait(&linewCond, &linewLock);
    pthread_mutex_unlock(&linewLock);

    for (uint32_t row = args->startRow; row < maxrows; ++row)
    {
        for (uint32_t col = args->startCol; col < maxcols; ++col)
        {
            double val = mat[row][col];
            // for (uint32_t i = 0; i < row; ++i)
            //     val += mat[i][col] * elimValues[row][i];
            elimValues[row][col] = val;
            mat[row][col]        = 0;
        }
    }
    // Done with block
    pthread_mutex_lock(&colLock);
    --(colWork[args->startRow / WORKLOAD]);
    pthread_cond_broadcast(&colCond);
    pthread_mutex_unlock(&colLock);

    // Signal work is done to main thread
    const union sigval retval = { .sival_int = args->tid };
    sigqueue(getpid(), SIGRTMIN, retval);
    pthread_exit(0);
}
void* NormalisationBlock(void* input)
{
    ptargs_s* args    = (ptargs_s*) input;
    uint32_t  maxrows = args->startRow + WORKLOAD;
    uint32_t  maxcols = args->startCol + WORKLOAD;

    for (uint32_t row = args->startRow; row < maxrows; ++row)
    {
        // Check if data is available
        pthread_mutex_lock(&lineLock);
        while (normzLine - 1 < row)
            pthread_cond_wait(&lineCond, &lineLock);
        pthread_mutex_unlock(&lineLock);

        // Normalise remaining values
        double inversediv = normzValues[row];
        for (uint32_t col = args->startCol; col < maxcols; ++col)
        {
            // Eliminate
            double val = mat[row][col];
            // for (uint32_t i = 0; i < row; ++i)
            //     val += mat[i][col] * elimValues[row][i];

            // Normalise
            mat[row][col] = val * inversediv;
        }
    }

    // Column block is done with work
    pthread_mutex_lock(&linewLock);
    ++(lineWork[(int) (args->startCol / WORKLOAD)]);
    pthread_cond_broadcast(&linewCond);
    pthread_mutex_unlock(&linewLock);

    // Signal work is done to main thread
    const union sigval retval = { .sival_int = args->tid };
    sigqueue(getpid(), SIGRTMIN, retval);
    pthread_exit(0);
}
void* BackSubstitution(void* input)
{
    ptargs_s* args    = (ptargs_s*) input;
    uint32_t  maxrows = args->startRow + WORKLOAD;
    uint32_t  maxcols = args->startCol + WORKLOAD;


    // Calculates the result


    // Signal work is done to main thread
    const union sigval retval = { .sival_int = args->tid };
    sigqueue(getpid(), SIGRTMIN, retval);
    pthread_exit(0);
}
void* TReady(void* input)
{
    ptargs_s*          args   = (ptargs_s*) input;
    const union sigval retval = { .sival_int = args->tid };
    sigqueue(getpid(), SIGRTMIN, retval);
    pthread_exit(0);
}

void work(void)
{
    printf("[d] Start\n");
    pthread_mutex_init(&linewLock, NULL);
    pthread_mutex_init(&lineLock, NULL);
    pthread_mutex_init(&colLock, NULL);
    pthread_cond_init(&colCond, NULL);
    pthread_cond_init(&linewCond, NULL);
    pthread_cond_init(&lineCond, NULL);
    normzLine = 0;
    for (uint32_t i = 0; i < MAT_SIZE / WORKLOAD; ++i)
        colWork[i] = i;
    memset(lineWork, 0, MAT_SIZE / WORKLOAD);

    pthread_t threads[THREAD_COUNT] = {};
    ptargs_s  args[THREAD_COUNT]    = {};

    siginfo_t data;
    sigset_t  sig;
    sigemptyset(&sig);
    sigaddset(&sig, SIGRTMIN);
    // Has to be blocking for sigwait to work
    pthread_sigmask(SIG_BLOCK, &sig, NULL);


    // Init
    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        args[i] = (ptargs_s){ .tid = i, .startRow = 0, .startCol = i * WORKLOAD };
        pthread_create(&threads[i], NULL, TReady, &args[i]);
    }

    for (uint32_t diagonal = 0; diagonal < MAT_SIZE / WORKLOAD; ++diagonal)
    {
        // Wait until worker is ready
        sigwaitinfo(&sig, &data);
        pthread_join(threads[data.si_int], NULL);

        // Wave start
        args[data.si_int].startCol = diagonal * WORKLOAD;
        args[data.si_int].startRow = diagonal * WORKLOAD;
        pthread_create(&threads[data.si_int], NULL, HybridBlock, &args[data.si_int]);

        // Wave right
        for (uint32_t col = diagonal + 1; col < MAT_SIZE / WORKLOAD; ++col)
        {
            sigwaitinfo(&sig, &data);
            pthread_join(threads[data.si_int], NULL);

            args[data.si_int].startCol = col * WORKLOAD;
            args[data.si_int].startRow = diagonal * WORKLOAD;
            pthread_create(&threads[data.si_int], NULL, NormalisationBlock, &args[data.si_int]);
        }
        // Wave down
        for (uint32_t row = diagonal + 1; row < MAT_SIZE / WORKLOAD; ++row)
        {
            sigwaitinfo(&sig, &data);
            pthread_join(threads[data.si_int], NULL);

            args[data.si_int].startCol = diagonal * WORKLOAD;
            args[data.si_int].startRow = row * WORKLOAD;
            pthread_create(&threads[data.si_int], NULL, EliminationBlock, &args[data.si_int]);
        }
    }

    // BARRIER
    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        if (sigwaitinfo(&sig, &data) == SIGRTMIN)
            pthread_join(threads[data.si_int], NULL);
    }
    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
        pthread_create(&threads[i], NULL, TReady, &args[i]);

    // Get result
    uint32_t blockertid = 0;
    for (int32_t diagonal = MAT_SIZE / WORKLOAD - 1; diagonal >= 0; --diagonal)
    {
        sigwaitinfo(&sig, &data);
        pthread_join(threads[data.si_int], NULL);

        if (data.si_int == blockertid)
        {
            // Shedule next line 2nd last row
        }
        // else cycle up until cols are done

        args[data.si_int].startCol = 0 /*MAT_SIZE - WORKLOAD * diagonal*/;  // TODO
        args[data.si_int].startRow = 0 /*MAT_SIZE - WORKLOAD * diagonal*/;  // TODO
        pthread_create(&threads[data.si_int], NULL, BackSubstitution, &args[data.si_int]);
    }

    // Ensure all threads have joined before exiting, else args will be void
    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        if (sigwaitinfo(&sig, &data) == SIGRTMIN)
            pthread_join(threads[data.si_int], NULL);
    }


    pthread_mutex_destroy(&linewLock);
    pthread_mutex_destroy(&lineLock);
    pthread_mutex_destroy(&colLock);
    pthread_cond_destroy(&colCond);
    pthread_cond_destroy(&linewCond);
    pthread_cond_destroy(&lineCond);
    printf("[d] All done\n");
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
