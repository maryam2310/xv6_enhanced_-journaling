x// sleepprog.c
#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(2, "Usage: sleepprog <ticks>\n");
    exit();
  }

  int ticks = atoi(argv[1]);

  if(ticks < 0){
    printf(2, "Error: ticks must be a non-negative integer\n");
    exit();
  }

  printf(1, "Sleeping for %d ticks...\n", ticks);
  sleep(ticks);
  printf(1, "Done sleeping!\n");

  exit();
}

