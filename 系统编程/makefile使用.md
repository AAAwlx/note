# makefile的使用

## 使用makefile的意义

当要运行一个大程序时，代码会分开成许多个文件。逐个编译十分的不方便。而makefile可以将一个工程中所有的代码一次性编译，十分方便

## 一个规则

makefile文件只能命名为makefile或是Makefile

可执行文件名：源文件名

​	（一定不要忘记缩进）编译命令

```makefile
hello:hello.c
	gcc hello.c -o hello
```

若是有多个文件时，会采用

```sh
gcc 1.c 2.c 3.c 4.c -o a.out
```

该命令进行连编。但是该命令存在一定的弊端。当该工程中只有一个文件发生更改时，其余文件也要一起被编译，这样会无故的消耗计算机的资源。因此需单独编译修改后的文件，再将它与其余文件链接

这时makefile可以这样写

```makefile
ALL:a.out#终极目标
a.out:1.o 2.o 3.o 4.o 
	gcc 1.o 2.o 3.o 4.o -o a.out
1.o:1.c
	gcc -c 1.c -o 1.o
2.o:2.c
	gcc -c 2.c -o 2.o
3.o:3.c
	gcc -c 3.c -o 3.o
4.o:4.c
	gcc -c 4.c -o 4.o
```

此时makefile还是比较繁琐，不智能，依旧需要调整

## 两个函数

src=$(wildcard * .c)

该函数的作用是找到含有makefile文件的目录下所有的含有.c的文件名并将他们存在scr这个列表中

obj=$(patsubst %.c ,%.o,$(src))

该函数的作用是把含有.c的文件全部替换为.o并存到obj列表中

clean函数

可以用来删除一些中间产生的无意义的文件，使用时在make后加上clean的指令

此时可将文件改为如下形式

```makefile
src=$(wildcard * .c)
obj=$(patsubst %.c ,%.o,$(src))
ALL:a.out#终极目标
a.out:$(obj) 
	gcc $(obj) -o a.out
1.o:1.c
	gcc -c 1.c -o 1.o
2.o:2.c
	gcc -c 2.c -o 2.o
3.o:3.c
	gcc -c 3.c -o 3.o
4.o:4.c
	gcc -c 4.c -o 4.o
clean:
	-rm -rf $(obj) a.out#将中间产生的文件删除
```

## 三个自动变量

$@ 表示规则中的目标

$< 表示规则中的第一个条件

$^ 表示规则中的全部条件

```makefile
src=$(wildcard * .c)
obj=$(patsubst %.c ,%.o,$(src))
ALL:a.out#终极目标
a.out:$(obj) 
	gcc $^ -o $@
1.o:1.c
	gcc -c $< -o $@
2.o:2.c
	gcc -c $< -o $@
3.o:3.c
	gcc -c $< -o $@
4.o:4.c
	gcc -c $< -o $@
clean:
	-rm -rf $(obj) a.out#将中间产生的文件删除
```



## 模式规则

%.o : %.c

​	gcc $^ -o $@

%.o代表.o文件，%.c代表.c文件

```makefile
src=$(wildcard * .c)
obj=$(patsubst %.c ,%.o,$(src))
ALL:a.out#终极目标
a.out:$(obj) 
	gcc $^ -o $@
%.o : %.c
	gcc -c $< -o $@
clean:
	-rm -rf $(obj) a.out#将中间产生的文件删除
```

当一个文件有多个依赖时模式规则容易产生混乱，这时需要用静态模式规则来指定给谁用模式规则

## 伪目标

.PHONY clean ALL

在一个工程中可能会有文件和ALL与clean重名，会导致make命令无法执行

当加入伪目标后便可正常运行

```makefile
src=$(wildcard * .c)
obj=$(patsubst %.c ,%.o,$(src))
ALL:a.out#终极目标
a.out:$(obj) 
	gcc $^ -o $@
%.o : %.c
	gcc -c $< -o $@
clean:
	-rm -rf $(obj) a.out#将中间产生的文件删除
.PHONY clean ALL
```

