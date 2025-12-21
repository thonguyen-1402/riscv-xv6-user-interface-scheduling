// user/quiet_worker.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("quiet_worker starting, PID=%d\n", getpid());  // ONE line only

  volatile int x = 0;
  for(;;){
    x++;           // burn CPU, no printing
  }
}
