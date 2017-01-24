#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "semaphores.h"

int errno;

int main(int argc, char** argv)
{    

    /*CHECK QTY OF ARGUMENTS*/
    if (argc != 2)
    {
        printf("Error: input filename expected.\n");
        exit(EXIT_ARGS);
    }

    int exitCode = EXIT_OK;
    
    /*OPEN INPUT FILE*/
    int fileinFD = open(argv[1], O_RDONLY);
    if(fileinFD == -1)
    {
        perror("Input file");
        exit(EXIT_FILEIN);
    }

    /*CREATE SEMAPHORE SET*/
    int semid = semget(ftok("semid.key", 3), 5, IPCFLAGS);
    if (semid == -1)
    {
        perror("Semget");
        exit(EXIT_SEMGET);
    }

    /*DEBUG REMOVE SEMAPHORES SET
    if (argv[2][0] == 'r')
    {
        if (semctl(semid, 0, IPC_RMID) == -1)
        {
            perror("Removing semaphores");
            exit(EXIT_SEMOP);
        }

        exit(88);
    }

    /*SINGLE SERVER SECTION*/
    struct sembuf singleton[2];
    singleton[0].sem_num = SRVSINGLSEM;
    singleton[0].sem_flg = 0;
    singleton[1].sem_num = SRVSINGLSEM;
    singleton[1].sem_flg = SEM_UNDO;

    singleton[0].sem_op = 0;
    singleton[1].sem_op = 1;

    /*TEST & LOCK*/
    if(semop(semid, &(singleton[0]), 2) == -1)
    {
        perror("Singleton semaphore");
        exit(EXIT_SEMOP);
    }

    /*SERVER_BUSY-CLIENT_BUSY SEMOP STRUCTURE*/
    struct sembuf busy[2];
    busy[0].sem_num = SERVERRDYSEM; //ServerBusy
    busy[0].sem_flg = SEM_UNDO; //ServerBusy
    busy[1].sem_num = CLIENTRDYSEM; //ClientBusy
    busy[1].sem_flg = 0; //ClientBusy

    /*SET SERVERRDYSEM TO 0*/
    if(semctl(semid, SERVERRDYSEM, SETVAL, (int)0) == -1)
    {
        perror("BusySemCtl");
        exit(EXIT_SEMCTL);
    }

    /*PRE-LOCK CLIENT'S ACTIVITY*/
    busy[0].sem_op = 1;
    if(semop(semid, &(busy[0]), 1) == -1)
    {
        perror("ServerBusy++ semaphore");
        exit(EXIT_SEMOP);
    }

    /*NO SEM_UNDO NEEDED FOR ServerBusy ANYMORE*/
    busy[0].sem_flg = 0; 

    /*ORDER OF EXECUTION SYNCHRONIZATON*/
    struct sembuf sync;
    sync.sem_num = SYNCSEM;
    sync.sem_flg = 0;

    /*CREATE SHMEM*/
    int key = ftok("shmid2.key", 2);
    int size = getpagesize();
    int shmid = shmget(key, size, IPCFLAGS);
    if (shmid == -1)
    {
        perror("Shmget");
        exit(EXIT_SHMGET);
    }

    /*ATTACH SHMEM*/
    void* buffer = NULL;
    if( (buffer = shmat(shmid, buffer, 0)) == (void*)-1 )
    {
        perror("Shmat");
        exit(EXIT_SHMAT);
    }
    
    /*SEND STRING WITH SHMEM*/
    /*LAST 4 BYTES IN SHMEM IS QTY OF BYTES READ.*/
    /*WHEN IT'S 0, SERVER IS DONE READING*/

    /*FIRST BURST*/ 
    int *qtyread = (int*)(buffer + size - sizeof(int));
    {
        *qtyread = read(fileinFD, buffer, size - sizeof(int));
        
        busy[0].sem_op = -1;
        if(semop(semid, &(busy[0]), 1) == -1)
        {
            perror("ServerBusy-- semaphore");
            exit(EXIT_SEMOP);
        }
    }

    /*RELEASE CLIENT*/
    sync.sem_op = -1;
    if(semop(semid, &sync, 1) == -1)
    {
        perror("ServerFirst semaphore");
        exit(EXIT_SEMOP);
    }

    /*CYCLE BURST*/
    while((*qtyread > 0) && (semctl(semid, CLTSINGLSEM, GETVAL) == 1))
    {
        /*RECIVE APPROVAL FROM CLIENT*/
        busy[1].sem_op = 0;
        if(semop(semid, &(busy[1]), 1) == -1)
        {
            perror("Client0 semaphore");
            exit(EXIT_SEMOP);
        }

        *qtyread = read(fileinFD, buffer, size - sizeof(int));


        if(semctl(semid, SERVERRDYSEM, GETVAL) == 0)
        {
            exitCode = EXIT_CYCLE;
            break;
        }
        
        /*RELEASE CLIENT'S ACTIVITY*/
        busy[0].sem_op = -1;
        busy[1].sem_op = 1;
        if(semop(semid, &(busy[0]), 2) == -1)
        {
            perror("ServerBusy-- & ClientBusy++ semaphore");
            exit(EXIT_SEMOP);
        }
    }

    /*DEATTACH SHMEM*/
    if(shmdt(buffer) == -1)
    {
        perror("Shmdt");
        exit(EXIT_SHMDT);
    }

    return exitCode;
}