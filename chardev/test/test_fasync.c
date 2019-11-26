#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void signalio_handler(int signum)
{
    printf("Receive a signal from globalmem, signalnum:%d\n", signum);
}

void main(void)
{
    int fd, oflags;
    fd = open("/dev/globalmem", O_RDWR, S_IRUSR | S_IWUSR);
    if (fd != -1){
        signal(SIGIO, signalio_handler);
        fcntl(fd, F_SETOWN, getpid());
        oflags = fcntl(fd, F_GETFL);
        fcntl(fd, F_SETFL, oflags | FASYNC);
        while(1){
            sleep(100);
        }
    }else{
        printf("Device open failed. \n");
    }
    
    return ;
}
