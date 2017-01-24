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
    int exitCode = EXIT_OK;

    /*OPEN SEMAPHORE SET*/
    int semid = semget(ftok("semid.key", 3), 5, IPCFLAGS);
    if (semid == -1)
    {
        perror("Semget");
        exit(EXIT_SEMGET);
    }

    /*SINGLE CLIENT SECTION*/
    struct sembuf singleton[2];
    singleton[0].sem_num = CLTSINGLSEM;
    singleton[0].sem_flg = 0;
    singleton[1].sem_num = CLTSINGLSEM;
    singleton[1].sem_flg = SEM_UNDO;

    singleton[0].sem_op = 0;
    singleton[1].sem_op = 1;

    /*AVAILABILITY TEST & SINGLETON LOCK*/
    if(semop(semid, &(singleton[0]), 2) == -1)
    {
        perror("Singleton semaphore");
        exit(EXIT_SEMOP);
    }

    /*SERVER_BUSY-CLIENT_BUSY SEMOP STRUCTURE*/
    struct sembuf busy[2];
    busy[0].sem_num = SERVERRDYSEM; //ServerBusy
    busy[0].sem_flg = 0; //ServerBusy
    busy[1].sem_num = CLIENTRDYSEM; //ClientBusy
    busy[1].sem_flg = SEM_UNDO; //ClientBusy

    /*SET CLIENTRDYSEM TO 0*/
    if(semctl(semid, CLIENTRDYSEM, SETVAL, (int)0) == -1)
    {
        perror("BusySemCtl");
        exit(EXIT_SEMCTL);
    }

    /*PRE-LOCK SERVER'S ACTIVITY*/
    busy[1].sem_op = 1;
    if(semop(semid, &(busy[1]), 1) == -1)
    {
        perror("ClientBusy++ semaphore");
        exit(EXIT_SEMOP);
    }

    /*NO SEM_UNDO NEEDED FOR ClientBusy ANYMORE*/
    busy[1].sem_flg = 0; 

    /*ORDER OF EXECUTION SYNCHRONIZATON*/
    struct sembuf sync;
    sync.sem_num = SYNCSEM;
    sync.sem_flg = SEM_UNDO;

    /*CREATE/OPEN SHMEM*/
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

    /*BLOCK OURSELVES*/
    sync.sem_op = 1;
    if(semop(semid, &sync, 1) == -1)
    {
        perror("ServerFirst semaphore");
        exit(EXIT_SEMOP);
    }

    /*WAIT FOR SERVER TO RELEASE*/
    sync.sem_op = 0;
    if(semop(semid, &sync, 1) == -1)
    {
        perror("ServerFirst semaphore");
        exit(EXIT_SEMOP);
    }

    /*RECIEVE STRING WITH SHMEM*/
    int *qtyread = (int*)(buffer + size - sizeof(int));
    while(semctl(semid, SRVSINGLSEM, GETVAL) == 1)
    {
        /*RECIVE APPROVAL FROM SERVER*/
        busy[0].sem_op = 0;
        if(semop(semid, &(busy[0]), 1) == -1)
        {
            perror("ServerBusy0 semaphore");
            exit(EXIT_SEMOP);
        }
        
        write(STDOUT_FILENO, buffer, *qtyread);
        
        if(semctl(semid, CLIENTRDYSEM, GETVAL) == 0)
        {
            exitCode = EXIT_CYCLE;
            break;
        }        

        /*RELEASE SERVER'S ACTIVITY*/
        busy[1].sem_op = -1;
        busy[0].sem_op = 1;
        if(semop(semid, &(busy[0]), 2) == -1)
        {
            perror("ServerBusy++ & ClientBusy-- semaphore");
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