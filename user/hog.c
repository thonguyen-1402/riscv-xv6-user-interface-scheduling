#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int n = 2;
  if (argc > 1)
    n = atoi(argv[1]);

  for (int i = 0; i < n; i++) {
    if (fork() == 0) {
      volatile unsigned long x = 0;
      for (;;)
        x++;
    }
  }

  // parent: wait forever (children never exit, so wait blocks)
  for (;;)
    wait(0);
}

