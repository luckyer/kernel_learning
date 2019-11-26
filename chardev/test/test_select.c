#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

const int buffer_len = 20;
#define GLOBAL_MAGIC 'g'
#define MEM_CLEAR       _IO(GLOBAL_MAGIC, 0)

int main(void)
{
    int fd, num ;
    char rd_ch[buffer_len];
    fd_set rfds, wfds;
    
    fd = open("/dev/globalmem", O_RDONLY | O_NONBLOCK);
    if (fd != 0){
        if (ioctl(fd, MEM_CLEAR, 0) < 0){
            printf("Ioctl failed. \n");
            return -1;
        }
        while(1){
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_SET(fd, &rfds);
            FD_SET(fd, &wfds);
            
            select(fd + 1, &rfds, &wfds, NULL, NULL);
            /*数据可获得*/
            if (FD_ISSET(fd, &rfds))
                printf("Poll monitor:can be read.\n");
            /*数据可写入*/
            if (FD_ISSET(fd, &wfds))
                printf("Poll monitor:can be written. \n");
			sleep(1);
        }
    }else{
        printf("Open device failed. \n");
    }
    
    return 0;
}
