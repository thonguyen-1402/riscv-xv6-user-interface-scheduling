// user/schedctl.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
usage(void)
{
  printf("usage:\n");
  printf("  schedctl policy rr [slice]\n");
  printf("  schedctl policy wrr|stride|mlfq\n");
  printf("  schedctl weight <pid> <w>\n");
  printf("  schedctl slice <pid> <ticks>\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  if (argc < 2)
    usage();

  // ----------------- policy -----------------
  if (strcmp(argv[1], "policy") == 0) {
    // allow: policy rr
    // allow: policy rr <slice>
    // allow: policy wrr|stride|mlfq
    if (argc < 3 || argc > 4)
      usage();

    int pol;
    if (strcmp(argv[2], "rr") == 0)
      pol = SCHED_RR;
    else if (strcmp(argv[2], "wrr") == 0)
      pol = SCHED_WEIGHTED_RR;
    else if (strcmp(argv[2], "stride") == 0)
      pol = SCHED_STRIDE;
    else if (strcmp(argv[2], "mlfq") == 0)
      pol = SCHED_MLFQ;
    else
      usage();

    if (setschedpolicy(pol) < 0) {
      printf("schedctl: setschedpolicy failed\n");
      exit(1);
    }

    // RR only: optional default slice for future processes
    if (argc == 4) {
      if (pol != SCHED_RR)
        usage();

      int slice = atoi(argv[3]);
      // pid==0 means "set RR default for future processes" in your kernel
      if (setschedslice(0, slice) < 0) {
        printf("schedctl: set RR default slice failed\n");
        exit(1);
      }
    }

    exit(0);
  }

  // ----------------- weight -----------------
  if (strcmp(argv[1], "weight") == 0) {
    if (argc != 4)
      usage();

    int pid = atoi(argv[2]);
    int w   = atoi(argv[3]);

    if (setschedweight(pid, w) < 0)
      printf("schedctl: setschedweight failed\n");
    exit(0);
  }

  // ----------------- slice ------------------
  if (strcmp(argv[1], "slice") == 0) {
    if (argc != 4)
      usage();

    int pid   = atoi(argv[2]);
    int slice = atoi(argv[3]);

    if (setschedslice(pid, slice) < 0)
      printf("schedctl: setschedslice failed\n");
    exit(0);
  }

  usage();
  return 0;
}

