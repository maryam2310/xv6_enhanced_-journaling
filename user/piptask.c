#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main() {
    int p[2];
    char buf[20];
   
   pipe (p);
   int pid = fork ();
   if (pid == 0){
      close(p[1]);
      read(p[0], buf ,sizeof(buf));
      printf("child received:%s",buf);
      close(p[0]);
}
 else{
     close(p[0]);
     write(p[1],"hello child",12);
     close(p[1]);
     wait(0);
}
exit(0);
