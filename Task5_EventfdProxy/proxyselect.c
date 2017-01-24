#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <math.h>

#define EXIT_OK 0
#define EXIT_ARG_FAILURE 1
#define EXIT_MALLOC 2
#define EXIT_WRITE 3
#define EXIT_READ 4
#define EXIT_FILEIN 5
#define EXIT_CHILDWR 6
#define EXIT_SELECT 7
#define EXIT_CHILDRD 8
#define EXIT_PIPE 9
#define EXIT_FORK 10
#define EXIT_EPOLL 11

#define PAGE_SIZE 0x1000

typedef struct
{
  int size; //size of buffer
  int isFull; //full flag
  char* buf; //pointer to the beginning of buffer
  char* end; //pointer to the end
  char* wr_ptr; //where data can be stored
  char* rd_ptr; //where data can be recieved from
} rbuff_t;

typedef struct
{
  rbuff_t rbuff;
  int fdin;
  int fdout;
} band_t;

void band_ctor(band_t *this, int size/*, int in, int out*/)
{
  this->rbuff.buf = NULL;
  this->rbuff.buf = (char*)malloc(size * sizeof(char));
  if(this->rbuff.buf == NULL)
  {
    perror("malloc in band_ctor");
    exit(EXIT_MALLOC);
  }
  this->rbuff.size = size;
  this->rbuff.isFull = 0;
  this->rbuff.wr_ptr = this->rbuff.buf;
  this->rbuff.rd_ptr = this->rbuff.buf;
  this->rbuff.end = this->rbuff.buf + size;
}

void band_dtor(band_t *this)
{
  free(this->rbuff.buf);
  this->rbuff.size = -1;
  this->rbuff.isFull = -1;
  this->rbuff.wr_ptr = NULL;
  this->rbuff.rd_ptr = NULL;
  this->rbuff.end = NULL;
  this->fdin = -1;
  this->fdout = -1;    
}

int band_write(band_t *this) //to the fdout
{
  /*if descriptor is invalid*/
  if(this->fdout < 0)
    return -2;

  int qtywr = 0;
  /*if read pointer is ahead || buffer is full*/
  if(this->rbuff.rd_ptr > this->rbuff.wr_ptr ||
    (this->rbuff.rd_ptr == this->rbuff.wr_ptr &&
     this->rbuff.isFull == 1))
      qtywr = write(this->fdout, this->rbuff.rd_ptr, 
                  this->rbuff.end - this->rbuff.rd_ptr);
  /*if write pointer is ahead*/
  else if(this->rbuff.wr_ptr - this->rbuff.rd_ptr > 0)
      qtywr = write(this->fdout, this->rbuff.rd_ptr,
                  this->rbuff.wr_ptr - this->rbuff.rd_ptr);
  
  /*if write returned error*/
  if (qtywr < 0)
  {
    perror("band_write");
    exit(EXIT_WRITE);
  }
  /*advance read pointer*/
  this->rbuff.rd_ptr += qtywr;

  /*if end pointer met, rewind*/
  if(this->rbuff.rd_ptr == this->rbuff.end)
    this->rbuff.rd_ptr = this->rbuff.buf;

  /*if any data were transmitted, buffer is not full*/
  if(qtywr != 0)
    this->rbuff.isFull = 0;

  return qtywr;
}

int band_read(band_t *this) //from the fdin
{
  /*if fdin is invalid*/
  if(this->fdin < 0)
    return -2;

  int qtyrd = 0;

  /*if write pointer ahead || (both at the same position 
                                            && Not full)*/
  if(this->rbuff.wr_ptr > this->rbuff.rd_ptr ||
    (this->rbuff.wr_ptr == this->rbuff.rd_ptr 
     && this->rbuff.isFull == 0))
    qtyrd = read(this->fdin, this->rbuff.wr_ptr,
      this->rbuff.end - this->rbuff.wr_ptr);
  /*if read pointer with data ahead*/
  else if(this->rbuff.wr_ptr < this->rbuff.rd_ptr)
    qtyrd = read(this->fdin, this->rbuff.wr_ptr,
      this->rbuff.rd_ptr - this->rbuff.wr_ptr);

  /*if read returned error*/
  if(qtyrd < 0)
  {
    perror("band_read");
    exit(EXIT_READ);
  }

  /*advance write pointer*/
  this->rbuff.wr_ptr += qtyrd;

  /*if nothing read, close descriptor*/
  if(qtyrd == 0)
  {
    close(this->fdin);
    this->fdin = -1;
  }

  /*if end met, rewind*/
  if(this->rbuff.wr_ptr == this->rbuff.end)
    this->rbuff.wr_ptr = this->rbuff.buf;

  /*if something read && both at the same position, set isFull*/
  if(qtyrd != 0 && this->rbuff.wr_ptr == this->rbuff.rd_ptr)
    this->rbuff.isFull = 1;

  return qtyrd;
}

int band_readReady(band_t *this)
{
  if(this->fdin > 0 && //read descriptor is alive
    (!this->rbuff.isFull || //not full
    this->rbuff.rd_ptr != this->rbuff.wr_ptr)) //or we didn't revome full flag
      return 1;
  else
      return 0;
}

int band_writeReady(band_t *this)
{
  if(this->fdout > 0 && //descriptor is alive
    (this->rbuff.isFull || //buffer is full
    this->rbuff.rd_ptr != this->rbuff.wr_ptr)) //buffer is not empty
      return 1;
  else
      return 0;
}

void band_checkAndClose(band_t *this)
{
  if(this->fdin < 0 && //read is dead
    this->fdout > 0 && //write is alive
    this->fdout != STDOUT_FILENO && //write is not stdout
    !this->rbuff.isFull && //buffer is empty
    this->rbuff.rd_ptr == this->rbuff.wr_ptr) //buffer is empty
  {
    close(this->fdout);
    this->fdout = -1;
  }
}

long getNum(char* str) //convert string into number
{
  char* endptr = NULL;
  errno = 0;  
  long n = strtol(str, &endptr, 10);
    
  if (errno != 0)
  {
    perror("Strtol");
    exit(EXIT_ARG_FAILURE);
  }
    
  if (*endptr != '\0')
  {
    printf("2nd arg is not a number\n");
    exit(EXIT_ARG_FAILURE);
  }

  return n;
}

void childRoutine(int in, int out) //read-write routine
{
  char buffer[PAGE_SIZE];
  int qtyread;
  while ((qtyread = read(in, buffer, PAGE_SIZE)) > 0) 
  {
    if (write(out, buffer, qtyread) < 0)
    {
      perror("Child Write");
      exit(EXIT_CHILDWR);
    }
  }

  if (qtyread < 0) 
  {
    perror("Child Read");
    exit(EXIT_CHILDRD);
  }

  return;
}

void makeNONBLOCK(int fd) //add to fd O_NONBLOCK
{
  int flags = fcntl(fd, F_GETFL);
  flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);
}

int getmax(int a, int b)
{
  if(a > b)
    return a;
  else
    return b;
}

int main(int argc, char** argv)
{
  /*CHECK ARGS QTY*/
  if(argc != 3)
  {
    printf("Usage: ./command <quantity_of_children> \
      <file_to_pass>\n");
    return EXIT_ARG_FAILURE;
  }

  /*get the number of children*/
  long chldQty = getNum(argv[1]);
  if (chldQty > 11)
  {
    printf("Quantity of children is too big\n");
    printf("Proceed at your own risk\n");
  }

  /*define the parent process*/
  int ActualParent = getpid();

  /*create array of pipes*/
  int fd[2 * chldQty - 1][2];
  for(int i = 0; i < 2 * chldQty - 1; i++)
  {
    pipe(fd[i]);
  }

  /*init bands array*/
  band_t band[chldQty];
  int bufsize = pow(3, chldQty) * PAGE_SIZE;
  for(int j = 0; j < chldQty; j++)
  {
    band_ctor(band + j, bufsize);
    bufsize /= 3;
  }

  pid_t pid = 0;
  
  /*Forkinig the first child explicitly*/
  pid = fork();
  if(pid == 0)
  {
    /*in first child*/

    /*close unnececary descriptors*/
    for(int i = 1; i < 2 * chldQty - 1; i++)
      {
        close(fd[i][0]);
        close(fd[i][1]);
      }
    close(fd[0][0]);

    /*define pipes to work with*/
    int pipein = open(argv[2], O_RDONLY);
    int pipeout = fd[0][1];
    if (pipein < 0)
    {
      perror("OPEN");
      exit(EXIT_FILEIN);
    }

    childRoutine(pipein, pipeout);

    /*close pipes on exit*/
    close(pipein);
    //pipein = -1;
    close(pipeout);
    //pipeout = -1;
  }
  else if (pid < 0)
  {
    perror("FORK");
    exit(EXIT_FORK);
  }
  else if (pid > 0)
  {
    /*in parent*/
    makeNONBLOCK(fd[0][0]);
    band[0].fdin = fd[0][0];
    //band[0].fdout = -1;
  }

  int pipein = 0;
  int pipeout = 0;
  for (long id = 1; (id < chldQty) && (getpid() == ActualParent); id++)
  {
    if((pid = fork()) == 0)
    {
      /*in ordinary child*/
      /*close unnececary descriptors*/
      for(int i = 0; i < 2 * chldQty - 1; i++)
        if((i != 2 * id) && (i != 2 * id - 1))
        {
          close(fd[i][0]);
          close(fd[i][1]);
        }

        close(fd[2 * id][0]);
        close(fd[2 * id - 1][1]);

        int pipein = fd[2 * id - 1][0];
        int pipeout = fd[2 * id][1];
        childRoutine(pipein, pipeout);

        //sleep(10);
        close(pipein);
        close(pipeout);
    }
    else if (pid < 0)
    {
      perror("FORK");
      exit(EXIT_FORK);
    }
    else
    {
      /*in parent*/

      /*set data in band array*/
      makeNONBLOCK(fd[2 * id - 1][1]);
      band[id - 1].fdout = fd[2 * id - 1][1];
      makeNONBLOCK(fd[2 * id][0]);
      band[id].fdin = fd[2 * id][0];
    }

  }

  if (pid != 0)
  {
    /*parentRoutine here*/

    /*initialize last connection to the stdout*/
    band[chldQty - 1].fdout = STDOUT_FILENO;

    /*close unncecery descriptors*/
    for(int i = 0; i < 2 * chldQty - 1; i++)
    {
      if(i % 2 == 0)
        close(fd[i][1]);
      else
        close(fd[i][0]);
    }

    /*sets for select*/
    fd_set wr_set;
    fd_set rd_set;

    int maxfd = 0;

    /*cycle*/
    while(1)
    {
      /*clear max value*/
      maxfd = 0;

      /*clear all sets*/
      FD_ZERO(&wr_set);
      FD_ZERO(&rd_set);

      /*add descriptors*/
      for(int i = 0; i < chldQty; i++)
      {
        int tempfd = 0;

        /*check fdin < 0 buffer is empty -> close fdout*/
        band_checkAndClose(&(band[i]));
        
        /*if bandswitch is ready to read*/
        if(band_readReady(&(band[i])))
        {
          tempfd = band[i].fdin;
          maxfd = getmax(maxfd, tempfd);
          FD_SET(tempfd, &rd_set);
        }

        /*if ordinary(not the last one) bandswitch is reay to write*/
        if((i != chldQty -1) && band_writeReady(&(band[i])))
        {
          tempfd = band[i].fdout;
          maxfd = getmax(maxfd, tempfd);
          FD_SET(tempfd, &wr_set);
        }
      }
      /*if smth had been added*/
      if(maxfd != 0)
      {
        if(select(maxfd + 1, &rd_set, &wr_set, NULL, NULL) == -1)
        {
          perror("select");
          exit(EXIT_SELECT);
        }
        /*begin to process from the end*/
        band_write(&(band[chldQty - 1]));
        fflush(stdout);
        for(int i = chldQty - 1; i >= 0; i--)
        {
          if(FD_ISSET(band[i].fdout, &wr_set))
            band_write(&(band[i]));
          if(FD_ISSET(band[i].fdin, &rd_set))
            band_read(&(band[i]));
        }
      }
      /*if no descriptors were added*/
      else
      {
        /*get data from the last buffer*/
        int qty = band_write(&(band[chldQty - 1]));
        /*if nothing read and fdin is closed -> exit cycle*/
        if(qty == 0 && band[chldQty - 1].fdin < 0)
          break;
      }
    }
    
    for(int i = 0; i < chldQty; i++)
    {
      band_dtor(&(band[i]));
    }
  }
    
  for (long i = 1; (i < chldQty) && (getpid() == ActualParent); i++)
  {
    wait(NULL);
  }

  return EXIT_OK;
}