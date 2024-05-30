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
