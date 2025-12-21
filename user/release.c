#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int n = releaseworkers();
  printf("released %d quiet_worker processes\n", n);
  exit(0);
}
