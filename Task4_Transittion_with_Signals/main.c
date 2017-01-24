#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <string.h>

#define EXIT_OK 0
#define EXIT_ARGS 1
#define EXIT_FORK 2
#define EXIT_FILEIN 3
#define EXIT_CHILD 4
#define EXIT_PARENT 5
#define EXIT_PRCTL 6
#define EXIT_SIGACTION 7
#define EXIT_LOG 8
//#define err(text) fprintf(log, "\n");

char buffer = 0, bitPosition = 0;
pid_t childPid;

void bit1(int signo)
{
    buffer |= (1 << bitPosition);
    bitPosition++;
    //printf("****************\nGOT 1\n");
    //printf("buffer = %d\n", buffer);
    //printf("bitPosition = %d\n****************\n", bitPosition);

    kill(childPid, SIGUSR1); //Ready
}

void bit0(int signo)
{
    bitPosition++;
    //printf("****************\nGOT 0 \n");
    //printf("buffer = %d\n", buffer);
    //printf("bitPosition = %d\n****************\n", bitPosition);
    kill(childPid, SIGUSR1); //Ready
}

void childDead(int signo)
{
    //printf("\nChild exited\n");
    exit(EXIT_CHILD);
}


void parentReaction(int signo)
{
}

void parentDead(int signo)
{
    //printf("\nParent exited\n");
    exit(EXIT_PARENT);
}

int main(int argc, char** argv)
{
    /*CHECK QTY OF ARGS*/
    if (argc != 2)
    {
        printf("Error: input filename expected.\n");
        exit(EXIT_ARGS);
    }

    int log = open("log.txt", O_RDWR);
    if(log == -1)
    {
        exit(EXIT_LOG);
    }

    /*BLOCK SIGUSR1, SIGUSR2, SIGCHLD*/
    sigset_t preblock;
    sigemptyset(&preblock);
    sigaddset(&preblock, SIGUSR1);
    sigaddset(&preblock, SIGUSR2);
    sigaddset(&preblock, SIGCHLD);
    sigprocmask(SIG_BLOCK, &preblock, NULL);
    
    struct sigaction act1, act0, chldExit;
    
    memset(&act0, 0, sizeof(act0));
    act0.sa_handler = bit0;
    if(sigaction(SIGUSR1, &act0, NULL) == -1)
    {
        perror("Sigaction");
        exit(EXIT_SIGACTION);
    }
    
    memset(&act1, 0, sizeof(act1));
    act1.sa_handler = bit1;
    if(sigaction(SIGUSR2, &act1, NULL) == -1)
    {
        perror("Sigaction");
        exit(EXIT_SIGACTION);
    }

    memset(&chldExit, 0, sizeof(chldExit));
    chldExit.sa_handler = childDead;
    if(sigaction(SIGCHLD, &chldExit, NULL) == -1)
    {
        perror("Sigaction");
        exit(EXIT_SIGACTION);
    }

    pid_t parentPid = getpid();

    childPid = fork();
    if(childPid == -1)
    {
        perror("Fork");
        exit(EXIT_FORK);
    }

    if(childPid == 0)
    {

        //sleep(10);
        /*IN CHILD*/
        /*MAKE PARENT TO SIGNAL ON DEATH*/
        if(prctl(PR_SET_PDEATHSIG, SIGUSR2) == -1)
        {
            perror("Prctl");
            exit(EXIT_PRCTL);
        }

        /*MAKE SURE THAT OUR PARENT IS ONE 
        WE CATCH WITH SIGUSR2*/
        if(getppid() != parentPid)
        {
            printf("Parent was dead before we are \
                able to register it with prctl\n");
            exit(EXIT_PARENT);
        }

        sigset_t empty;
        sigemptyset(&empty);
        /*OPEN INPUT FILE*/
        int fileinFD = open(argv[1], O_RDONLY);
        if(fileinFD == -1)
        {
            perror("Input file");
            exit(EXIT_FILEIN);
        }

        /*HELL A LOT OF INITS*/
        struct sigaction response, parentExit;
        memset(&response, 0, sizeof(response));
        response.sa_handler = parentReaction;
        if(sigaction(SIGUSR1, &response, NULL) == -1)
        {
            perror("Sigaction");
            exit(EXIT_SIGACTION);
        }

        memset(&parentExit, 0, sizeof(parentExit));
        parentExit.sa_handler = parentDead;
        if(sigaction(SIGUSR2, &parentExit, NULL) == -1)
        {
            perror("Sigaction");
            exit(EXIT_SIGACTION);
        }

        char buff = 0;
        int i = 0;

        /*TRANSMITTING GOES FROM RIGHT TO LEFT*/
        while (read(fileinFD, &buff, sizeof(char)))
        {
            for(i = 0; i < sizeof(char) * 8; i++)
            {
                if ((buff & (1 << i)) == 0)
                    /*IF BIT IS 0*/
                    kill(parentPid, SIGUSR1);
                else
                    /*IF BIT IS 1*/
                    kill(parentPid, SIGUSR2);
            
                /*AWAITING RESPONSE FROM PARENT*/
                sigsuspend(&empty);
            }
        }
    }
    else
    {
        /*IN PARENT*/
        //exit(123);
        sigset_t blank;
        sigemptyset(&blank);

        /*RECIEVE SIGNALS FROM CHILD*/
        while(1)
        {
            if(bitPosition == 8)//filled buffer
            {
                write(STDOUT_FILENO, &buffer, sizeof(char));
                fflush(stdout);
                bitPosition = 0;
                buffer = 0;
            }
            /*SEND APPROVAL*/
            sigsuspend(&blank);
        }
    }

    return EXIT_OK;
}