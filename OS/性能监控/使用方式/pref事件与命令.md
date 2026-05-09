# perf事件

perf 是 Linux 性能分析工具，常用于收集和分析系统的性能数据。perf 事件（perf events）是由硬件或软件生成的事件，这些事件用于收集有关系统性能的信息。下面详细介绍 perf 事件的概念及其在性能分析中的应用。

perf 事件的类型
perf 事件主要分为以下几类：

硬件事件（Hardware Events）：

硬件性能计数器事件：由 CPU 内部的硬件性能计数器生成。这些事件包括指令计数、缓存命中和未命中、分支预测成功与失败等。
例子：
cycles：CPU 时钟周期数。
instructions：已执行的指令数。
cache-references：缓存访问次数。
cache-misses：缓存未命中次数。
软件事件（Software Events）：

由内核或软件生成，用于捕捉软件层面的性能信息。
例子：
cpu-clock：任务在 CPU 上花费的时间。
task-clock：所有 CPU 上花费的时间总和。
page-faults：页面错误次数。
Tracepoint 事件：

内核中的静态探测点，用于捕捉特定的内核行为。
例子：
sched:sched_switch：上下文切换事件。
block:block_rq_issue：块设备请求发起事件。
自定义事件（Custom Events）：

用户可以通过 perf_event_open 系统调用定义自定义事件。

```c
//对于perf_event_open系统调用的包装, libc里面不提供, 要自己定义
static int perf_event_open(struct perf_event_attr *evt_attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
    int ret;
    ret = syscall(__NR_perf_event_open, evt_attr, pid, cpu, group_fd, flags);
    return ret;
}
```
*struct perf_event_attr evt_attr:

指向 perf_event_attr 结构体的指针。这个结构体定义了性能事件的配置，包括事件类型、采样频率、排除选项等。
pid_t pid:

进程ID，用于指定要监控的进程。
如果 pid 为 0，则监控当前进程。
如果 pid 为正数，则监控指定的进程。
如果 pid 为 -1，则在系统范围内监控（与 cpu 结合使用）。
如果 pid 为 -2，则监控当前线程的所有线程组成员。
int cpu:

指定要监控的CPU。
如果 cpu 为 -1，则监控所有CPU。
如果 cpu 为正数，则监控指定的CPU。
int group_fd:

用于事件分组的文件描述符。
如果 group_fd 为 -1，则事件不属于任何组。
如果 group_fd 为正数，则事件属于由该文件描述符表示的事件组。
unsigned long flags:

标志位，用于进一步配置事件的行为。
常见的标志包括 PERF_FLAG_FD_CLOEXEC（在 execve 后自动关闭文件描述符）等。

在linux上提供perf指令对系统性能进行分析

1. Perf top 实时显示系统/进程的性能统计信息
2. Perf stat 分析系统/进程的整体性能概况
3. Perf record 记录一段时间内系统/进程的性能时间
4. Perf Report 读取perf record生成的数据文件，并显示分析数据