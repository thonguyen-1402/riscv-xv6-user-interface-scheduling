#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void block_for_ticks(int ticks)
{
  int pid = fork();
  if(pid == 0){
    uint64 start = uptime();
    while(uptime() - start < ticks)
      ;
    exit(0);
  }
  wait(0);
}

void
busy(char *tag)
{
  int i = 0;
  for(;;){
    i++;
    if(i % 10000000 == 0){
      printf("%s: i=%d\n", tag, i);
    }
  }
}

int
main(void)
{
  printf("scheddemo: set policy to weighted RR\n");
  setschedpolicy(SCHED_WEIGHTED_RR);

  int pidA = fork();
  if(pidA == 0){
    setschedweight(getpid(), 1);
    busy("A");
    exit(0);
  }

  int pidB = fork();
  if(pidB == 0){
    setschedweight(getpid(), 1);
    busy("B");
    exit(0);
  }

  printf("parent: A=%d, B=%d\n", pidA, pidB);

  block_for_ticks(100);
  printf("parent: boosting A\n");
  setschedweight(pidA, 5);

  block_for_ticks(200);
  printf("parent: boosting B, throttling A\n");
  setschedweight(pidB, 6);
  setschedweight(pidA, 1);

  block_for_ticks(500);

  exit(0);
}

