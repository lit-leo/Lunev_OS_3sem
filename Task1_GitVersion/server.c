#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>

int errno;

void sig_handler(int signo)
{
    if (signo == SIGPIPE)
        exit(7);
}

int main(int argc, char** argv)
{
    signal(SIGPIPE, sig_handler);
    /*TRANSFER PID TO CHAR STRING*/
    char fifoName[sizeof(long) + 1];
    sprintf(fifoName, "%ld", (long)getpid());
    printf("%s\n\n", fifoName);

    /*CHECK QTY OF ARGUMENTS*/
    if (argc != 2)
    {
        printf("Error: input filename expected.\n");
        exit(1);
    }

    /*OPEN INPUT FILE*/
    int fileinFD = open(argv[1], O_RDONLY);
    if(fileinFD == -1)
    {
        perror("Input file");
        exit(2);
    }

    /*CREATE AND OPEN PRIVATE FIFO FOR TEST*/
    if (mkfifo(fifoName, 0777) != 0)
    {
        perror("Private fifoName based on pid is not unique");
        exit(3);
    }

    /*OPEN IN O_WRONLY | O_NONBLOCK*/
    int getFifoFD = open(fifoName, O_RDWR | O_NONBLOCK);
    //printf("fifoFD = %d\n", fifoFD);
    int stableFifoFD = open(fifoName, O_WRONLY);
    close(getFifoFD);

    /*CREATE OR OPEN COMMON CONTROL FIFO*/
    if (mkfifo("Control", 0777) != 0)
    {
        printf("Control fifo file found\n");
    }

    int control_fifoFD = open("Control", O_WRONLY);
    if(control_fifoFD == -1)
    {
        perror("Control fifo opening");
        exit(5);
    }

    /*TRANSFER PRIVATE FIFO NAME TO CLIENT*/
    write(control_fifoFD, fifoName, sizeof(long) + 1);
    close(control_fifoFD);

    /*SIMULATING DELAY || GIVING TIME TO CONNECT*/
    sleep(3);

    char buffer[PIPE_BUF];
    int readOK = 1;
    while(readOK > 0)
    {
        readOK = read(fileinFD, buffer, PIPE_BUF);
        //printf("STR: %s\n", buffer);
        write(stableFifoFD, buffer, readOK);
    }
    
    close(fileinFD);
    close(stableFifoFD);

    return 0;
}
