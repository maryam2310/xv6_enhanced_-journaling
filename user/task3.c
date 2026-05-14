#include "kernel/types.h"
#include"kernel/stat.h"
#include"user/user.h"
int main(){
int p[2];
char buffer[20];
if (pipe(p) < 0){
printf("pip failed");
exit(1);
}
int pid = fork();
if (pid < 0){
printf("fork faild");
exit(1);
}
if (pid == 0){
close(p[1]);
read(p[0], buffer ,sizeof(buffer));
printf("child recived" ,buffer);
close (p[0]);
}else{ close(p[0]);
char massage[]="hello from parent";
write(p[1],massage,sizeof(massage));
close(p[1]);
wait(0);
}
exit(0);
}
