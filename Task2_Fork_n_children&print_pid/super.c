#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define EXIT_ARG_FAILURE -2
#define EXIT_DEL_FAILURE 2
#define EXIT_SND_FAILURE 3
#define EXIT_RCV_FAILURE 4

int errno;

struct msgbuf
    {
        long mtype;
        char mtext[sizeof(long)];    
    };

int main(int argc, char** argv)
{
    /*CHECK QTY OF ARGS*/
    if (argc != 2)
    {
        printf("Usage: ./command <quantity_of_children>\n");
        return EXIT_ARG_FAILURE;
    }
    
    /*GET NUMBER OF CHILDREN*/
    char* endptr = NULL;
    errno = 0;  
    long num = strtol(argv[1], &endptr, 10);
      
    if (errno != 0)
    {
        perror("Strtol:");
        return errno;
    }
        
    if (*endptr != '\0')
    {
        printf("2nd arg is not a number\n");
        return EXIT_ARG_FAILURE;
    }
    
    /*DEFINE THE PARENT PROCESS*/
    int ActualParent = getpid();
    //printf("ActualParent = %d\n", ActualParent);
    
    int QueueNumber = 0;

    /*SET UP MESSAGE QUEUE*/
    int qid = msgget(IPC_PRIVATE, 0777);
    long ParentFlag = num + 2;


    /*FORK N CHILDREN*/
    long i = 0;
    for (i = 0; (i < num) && (getpid() == ActualParent); i++)
    {
        if(fork() == 0)
        {
            /*IN CHILD*/
            
            /*EVERY CHILD KNOWS ITS QueueNumber*/
            QueueNumber = i + 1;
        }

    }
    
    long pid;
    if ( (pid = getpid()) == ActualParent)
    {
        /*IN PARENT*/

        /*INITIALIZE MSG*/
        struct msgbuf msg;
        //printf("\n");
        /*HANDLE FIRST CHILD*/
        //for (i = 1; i < num + 1; i++)
        {
            /*PREPARE MSG TO SEND/RECIVE*/
            msg.mtype = 1;

            /*SEND APPROVAL*/
            if (msgsnd(qid, (void*)&msg, 0, 0 | IPC_NOWAIT) == -1)
            {
                /*if (errno == EAGAIN)
                    printf("Message queue is full\n");*/
                perror("Message sending at parent");
                exit(EXIT_SND_FAILURE);
            }

            //printf("%d ", i);

            /*RECIEVE SYNC MSG*/
            if (msgrcv(qid, (void*)&msg, 0, num + 1, 0) == -1)
            {
                perror("Message recieving at parent");
                exit(EXIT_RCV_FAILURE);
            }
        }

        /*KILL MSG QUEUE*/
        if (msgctl(qid, IPC_RMID, NULL) == -1)
        {
            perror("Deleting queue in parent");
            exit(EXIT_DEL_FAILURE);
        }


    }
    else
    {
        /*IN CHILD*/

        /*PREPARE MSG TO SEND/RECIVE*/
        struct msgbuf msg;

        /*RECIEVE APPROVAL*/
        if (msgrcv(qid, (void*)&msg, 0, QueueNumber, 0) == -1)
        {
            perror("Message recieving at child");
            exit(EXIT_RCV_FAILURE);
        }

        /*PRINT IT, FINALY*/
        printf("%d ", QueueNumber);
        fflush(stdout);

        /*SEND SYNC MSG*/
        msg.mtype = QueueNumber + 1;
        if (msgsnd(qid, (void*)&msg, 0, 0) == -1)
        {
            perror("Sync Message sending at child");
            exit(EXIT_SND_FAILURE);
        }

    }

    return 0;
}
