#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>

int errno;

int main(int argc, char** argv)
{
    /*OPEN CONTROL FIFO*/
    int control_fifoFD = open("Control", O_RDONLY);
    if(control_fifoFD == -1)
    {
        perror("Control fifo opening");
        exit(4);
    }

    /*RECIEVE PRIVATE FIFO NAME*/
    char fifoName[sizeof(long) + 1];
    read(control_fifoFD, fifoName, sizeof(long));
    //printf("%s\n", fifoName);

    close(control_fifoFD);

    /*OPEN PRIVATE FIFO*/
    int fifoFD = open(fifoName, O_RDONLY /*| O_NONBLOCK*/);
    //sleep(5);
    //printf("FIFO is %d\n", fifoFD);

    if(fifoFD == -1)
    {
        perror("Fifo opening");
        exit(2);
    }
    
    /*GET SIZE OF TRANSISSION
    int testmem = 0;
    int testread = read(fifoFD, &testmem, sizeof(int));
    printf("SIZE IS %d\n", testmem);*/

    char outbuf[PIPE_BUF];
        
    int readOK = 1;
    while(readOK > 0)
    {
        readOK = read(fifoFD, outbuf, PIPE_BUF);
        //perror("Read form fifo");
        //printf("STR: %s\n", outbuf);
        //sleep(2);
        write(STDOUT_FILENO, outbuf, readOK);
    }
    printf("\n");

    close(fifoFD);

    unlink(fifoName);

    return 0;
}