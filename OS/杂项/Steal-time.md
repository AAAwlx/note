在 Linux 内核和虚拟化环境中，steal-time 是一个关键指标，用于衡量虚拟机（VM）的 CPU 资源被宿主（Host）或其他虚拟机强制占用的时间。它反映了虚拟化环境中 CPU 资源的竞争情况，直接影响虚拟机的性能表现。

Steal-time 的定义

steal-time 是指虚拟机（Guest OS）本应获得的 CPU 时间，但被宿主机（Hypervisor）或其他虚拟机抢占的时长。  

例如：在一个物理 CPU 上运行多个虚拟机时，宿主机需要调度这些虚拟机分时共享 CPU。如果宿主机将 CPU 时间分配给其他虚拟机，当前虚拟机的 CPU 时间就被“偷走”（stolen）。

为什么需要统计 Steal-time？

在虚拟化环境中：
虚拟机无法独占 CPU：它看到的 CPU 是虚拟化的，实际运行时间由宿主机分配。

性能问题的根源：如果 steal-time 过高，说明虚拟机因资源竞争无法获得足够的 CPU 时间，可能导致：

应用延迟增加。

吞吐量下降。

调度器误判（如认为 CPU 空闲而降低频率，进一步恶化性能）。

通过统计 steal-time，内核和调度器可以：
更准确地计算真实 CPU 利用率（排除被偷走的时间）。

调整调度策略（如避免在 steal-time 高时误触发负载均衡）。

优化频率调控（如 CPUFreq 避免在 steal-time 高时降频）。

Steal-time 的统计方式

（1）硬件支持
KVM/Xen 等虚拟化平台通过 paravirtualized clocks（如 kvm-clock）向虚拟机报告 steal-time。

虚拟机内核通过读取宿主提供的 steal-time 计数器（如 MSR_KVM_STEAL_TIME）获取数据。

（2）Linux 内核中的实现
struct rq：每个 CPU 的运行队列包含 steal_time 字段。

cpu_util_irq()：在计算 CPU 利用率时，steal-time 被归类为和 IRQ 类似的“不可压缩”开销（通过 cpu_util_irq() 统计）。

Steal-time 的影响场景

（1）CPU 利用率计算
在 effective_cpu_util() 函数中，steal-time 和 IRQ 一起被处理：

    irq = cpu_util_irq(rq); // 包含 steal-time
  util = scale_irq_capacity(util, irq, scale); // 调整任务利用率
  
如果不调整，任务利用率会被高估（因为 steal-time 导致实际可用 CPU 时间减少）。

（2）调度器行为
CFS 调度器：高 steal-time 可能导致任务运行时间延长（因为实际获得的 CPU 时间减少）。

实时任务（RT/DL）：可能因 steal-time 错过截止时间（deadline），需通过 min 参数强制保留资源。

（3）频率调控（CPUFreq）
如果 steal-time 高，effective_cpu_util() 返回的利用率可能降低，导致 CPU 降频（但实际是宿主机抢占了资源，降频会进一步恶化性能）。

因此，内核会通过 irq + steal-time 调整，避免低估负载。

如何监控 Steal-time？

（1）通过 top 或 vmstat

$ vmstat 1
procs -----------memory---------- ---swap-- -----io---- -system-- ------cpu-----
b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st

0      0 123456  78900 456789    0    0    10    20  100  200 10  5 80  0  5

st 列：表示 steal-time 占比（上图最后一列的 5%）。

（2）通过 /proc/stat

$ cat /proc/stat
cpu  1000 200 300 4000 50 0 10 5 0 0

第 8 列（steal）表示累计 steal-time（单位：jiffies）。

（3）专用工具（如 sar）

$ sar -v 1
Linux 5.4.0-xx (...)   06/10/2023   _x86_64_    (4 CPU)
...
12:00:01 AM   steal   %steal
12:00:02 AM     50    5.00

高 Steal-time 的解决方案

如果 steal-time 持续较高（如 >10%），说明虚拟机资源不足，可以：
增加虚拟 CPU（vCPU）数量：减少单个 vCPU 的竞争。

调整宿主机调度策略：如绑定 vCPU 到物理核（CPU pinning）。

优化负载均衡：避免将多个高负载虚拟机调度到同一物理 CPU。

升级硬件：使用更多物理 CPU 或支持嵌套虚拟化的硬件。

总结
Steal-time 是虚拟机被宿主机或其他虚拟机抢占的 CPU 时间。

它影响 Linux 内核的利用率计算、调度和频率调控，需通过 effective_cpu_util() 等函数特殊处理。

监控和优化 steal-time 对虚拟化环境的性能至关重要。