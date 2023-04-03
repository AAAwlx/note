# FIFO

## 创建FIFO

函数原型

```c
mkfifo(char * pathname,int mode);
```

参数一：文件名

参数二：权限

作用：可以实现无血缘关系的进程间的通信，支持多个读端写端

## 使用示例

读端

```c
#include<stdio.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<fcntl.h>
#include<string.h>
#include <stdlib.h>
#define SIZE 1024
int main()
{
    char buffer[SIZE];
    int fd;
    fd=open("/tmp/my_fifo",O_RDONLY);
    if(fd<0){
		perror("open");
		exit(1);
	}
    while(1){
        int len=read(fd,buffer,sizeof(buffer));
        write(STDOUT_FILENO,buffer,len);
        sleep(3);
    }
    close(fd);
    return 0;
}
```

写端

```c
#include<stdio.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<fcntl.h>
#include<string.h>
#include <stdlib.h>
#define SIZE 1024
int main()
{
	int fd;
	char buffer[SIZE];
	mkfifo("/tmp/my_fifo",0666);
	fd=open("/tmp/my_fifo", O_WRONLY);
	if(fd<0){
		perror("open");
		exit(1);
	}
	int i=0;
	while(1){
		sprintf(buffer,"嘿嘿！我是%d号\n",i++);
		write(fd,buffer,strlen(buffer));
		sleep(1);
	}
	close(fd);
	return 0;
}
```

## 实现原理

FIFO实质上为消息队列，具有先进先出的特性。