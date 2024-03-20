

# shell的实现

## shell可实现的基本功能

实现 管道 (也就是 |)

实现 输入输出重定向(也就是 < > >>)

实现 后台运行（也就是 & ）

实现 cd，要求支持能切换到绝对路径，相对路径和支持 cd -

屏蔽一些信号（如 ctrl + c 不能终止）

记录历史命令

## 任然存在的不足

1.无法编写shell脚本

2.无法处理不含空格的命令

## 命令示例

重定向写

```shell
echo ABCDEF > ./1.txt
```

重定向读

```sh
python < ./1.py 
```

重定向读与写

```shell
grep "rzj" > 2.txt < 1.txt
```

管道

```
cat 1.txt | awk '{print $1}' | sort | uniq -c | sort -r -n | head -n 5
```

重定向与管道

```sh
ls -a -l | grep abc | wc -l > 2.txt
 python < ./1.py | wc -c
```

后台执行

## shell的基本思路与部分代码

### 主函数部分

![](/home/wlx/图片/博客用图/shell1.png)

主函数部分应该调用包含有以下功能的函数

1.屏蔽ctrl c  ctrl t信号

2.打印提示符

3.命令的读入

4.解析命令行，将每条指令分开

5.判断指令类型

6.记录历史命令

记录历史命令需要用到readline库

```shell
gcc myshell.c -o myshell -lreadline     
```

主函数部分

```c
int main()
{
	read_history(NULL);
    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);//屏蔽ctrl c与ctrl t
	while(1){//循环等待用户输入
		Printf();//打印用户信息
		char *command=readline(NULL);//读入命令
		if (commod == NULL) // 当没有命令读入时跳到下一步
    	{
      		printf("\n");
      		continue;
    	}
		char *argv[MAX] = {NULL};
    	arg**=Stork(command);//将指令分割
		isdo(argv);//根据所含有的符号判断需要执行哪种指令
		add_history(commod);//记录历史命令
    	write_history(NULL);
		free(command);//释放
	}
}
```

### 重定向

![](/home/wlx/图片/博客用图/shell2.png)

```c
void mydup(char *argv[])
{
  char *strc[MAX] = {NULL};
  int i = 0;
  while (strcmp(argv[i], ">"))
  {
    strc[i] = argv[i];
    i++;
  }
  int number = i; // 重定向前面参数的个数
  int flag = isdo(argv, number);
  i++;
  // 出现 echo "adcbe" > test.c  这种情况
  int fdout = dup(1);                                         // 让标准输出获取一个新的文件描述符
  int fd = open(argv[i], O_WRONLY | O_CREAT | O_TRUNC, 0666); // 只写模式|表示如果指定文件不存在，则创建这个文件|表示截断，如果文件存在，并且以只写、读写方式打开，则将其长度截断为0。
  dup2(fd, 1);
  pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    exit(1);
  }
  else if (pid == 0) // 子进程
  {
    if (flag == 3) // 管道'|'
    {
      callCommandWithPipe(strc, number);
    }
    else
      execvp(strc[0], strc);
  }
  else if (pid > 0)
  {
    if (pass == 1)
    {
      pass = 0;
      printf("%d\n", pid);
      return;
    }
    waitpid(pid, NULL, 0);
  }
  dup2(fdout, 1);
}
```

### 管道

![](/home/wlx/图片/博客用图/shell3.png)

```c
void callCommandWithPipe(char *argv[], int count)
{
  pid_t pid;
  int ret[10];    // 存放每个管道的下标
  int number = 0; // 统计管道个数
  for (int i = 0; i < count; i++)
  {
    if (!strcmp(argv[i], "|"))
    {
      ret[number++] = i;
    }
  }
  int cmd_count = number + 1; // 命令个数
  char *cmd[cmd_count][10];
  for (int i = 0; i < cmd_count; i++) // 将命令以管道分割存放组数组里
  {
    if (i == 0) // 第一个命令
    {
      int n = 0;
      for (int j = 0; j < ret[i]; j++)
      {
        cmd[i][n++] = argv[j];
      }
      cmd[i][n] = NULL;
    }
    else if (i == number) // 最后一个命令
    {
      int n = 0;
      for (int j = ret[i - 1] + 1; j < count; j++)
      {
        cmd[i][n++] = argv[j];
      }
      cmd[i][n] = NULL;
    }
    else
    {
      int n = 0;
      for (int j = ret[i - 1] + 1; j < ret[i]; j++)
      {
        cmd[i][n++] = argv[j];
      }
      cmd[i][n] = NULL;
    }
  }                                // 经过上述操作，我们已经将指令以管道为分隔符分好,下面我们就可以创建管道了
  int fd[number][2];               // 存放管道的描述符
  for (int i = 0; i < number; i++) // 循环创建多个管道
  {
    pipe(fd[i]);
  }
  int i = 0;
  for (i = 0; i < cmd_count; i++) // 父进程循环创建多个并列子进程
  {
    pid = fork();
    if (pid == 0) // 子进程直接退出循环，不参与进程的创建
      break;
  }
  if (pid == 0) // 子进程
  {
    if (number)
    {
      if (i == 0) // 第一个子进程
      {
        dup2(fd[0][1], 1); // 绑定写端`
        close(fd[0][0]);   // 关闭读端
        // 其他进程读写端全部关闭
        for (int j = 1; j < number; j++)
        {
          close(fd[j][1]);
          close(fd[j][0]);
        }
      }
      else if (i == number) // 最后一个进程
      {
        dup2(fd[i - 1][0], 0); // 打开读端
        close(fd[i - 1][1]);   // 关闭写端
                             // 其他进程读写端全部关闭
        for (int j = 0; j < number - 1; j++)
        {
          close(fd[j][1]);
          close(fd[j][0]);
        }
      }
      else // 中间进程
      {
        dup2(fd[i - 1][0], 0); // 前一个管道的读端打开
        close(fd[i - 1][1]);   // 前一个写端关闭
        dup2(fd[i][1], 1);     // 后一个管道的写端打开
        close(fd[i][0]);       // 后一个读端关闭
        // 其他的全部关闭
        for (int j = 0; j < number; j++)
        {
          if (j != i && j != (i - 1))
          {
            close(fd[j][0]);
            close(fd[j][1]);
          }
        }
      }
    }

    execvp(cmd[i][0], cmd[i]); // 执行命令
    perror("execvp");
    exit(1);
  }
```

### 组合命令

![](/home/wlx/图片/博客用图/shell4.png)

根据整理出的规律应该先判断 > 再判断 | 最后判断 <

循环挨个检查命令符号的顺序，在各种操作中再根据命令的规律再不同的操作中再次调用isdo函数得到新的flag决定后续操作的执行

```c
int isdo(char *argv[], int count)
{
  int flag = 10, i;
  if (argv[0] == NULL)
    return 0;
  if (strcmp(argv[0], "cd") == 0)
  {
    flag = 1;
  }
  for (i = 0; i < count; i++)
  {
    if (strcmp(argv[i], ">") == 0)
      flag = 2;
    if (strcmp(argv[i], "|") == 0)
      flag = 3;
    if (strcmp(argv[i], ">>") == 0)
      flag = 4;
    if (strcmp(argv[i], "<") == 0)
      flag = 5;
    if (strcmp(argv[i], "<<") == 0)
      flag = 6;
    if (strcmp(argv[i], "&") == 0)
    {
      pass = 1;
      argv[i] = NULL;
    }
  }
  return flag;
}
```

