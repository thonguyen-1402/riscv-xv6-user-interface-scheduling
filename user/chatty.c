#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  char c;
  for(;;){
    int n = read(0, &c, 1);     // blocks waiting for input
    if(n > 0)
      write(1, &c, 1);          // echo it
  }
}

