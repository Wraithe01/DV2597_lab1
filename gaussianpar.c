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

#define MAT_SIZE 4096
#define THREAD_COUNT 16
// Amount of bytes to work per row x column for each checkerboard slot which a single thread will
// work with. This must be of the format of 2^x


typedef double matrix[MAT_SIZE][MAT_SIZE];


int   N;      /* matrix size		    */
int   maxnum; /* max number of element*/
char* Init;   /* matrix init type	    */
int   PRINT;  /* print switch		    */

matrix mat;         /* matrix A		        */
double b[MAT_SIZE]; /* vector b             */
double y[MAT_SIZE]; /* vector y             */


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
    work();
    if (PRINT == 1)
        Print_Matrix();
    return 0;
}

typedef struct ptargs
{
    //
} ptargs_s;
void* GaussianElim(void* input)
{
    ptargs_s args = *(ptargs_s*) input;
    pthread_exit(NULL);
}

void work(void) {}

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
