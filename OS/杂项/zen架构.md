在AMD Zen架构中，调度域（Scheduling Domains）的划分紧密结合了其独特的CCX（Core Complex）和NUMA（非统一内存访问）设计。以下是关键划分逻辑及优化考量：

1. Zen架构的物理层级

Zen架构的核心拓扑如下：
• CCX（Core Complex）：每个CCX包含4个物理核心（Zen 1/2）或8个核心（Zen 3+），共享L3缓存。

• CCD（Core Complex Die）：单个芯片（Die）包含1-2个CCX（Zen 2后通常为1个CCX per CCD）。

• NUMA节点：单个CPU封装可能包含多个CCD，通过Infinity Fabric互连，形成NUMA结构。

2. Linux调度域的层级划分

Linux内核根据硬件拓扑自动构建调度域层级，典型划分如下（以Zen 3为例）：

a. SMT层级（最底层）

• 范围：单个物理核心内的SMT线程（如Zen的SMT2）。

• 调度策略：

  • 共享L1/L2缓存，优先在空闲超线程上调度任务。

  • 避免跨CCX迁移线程以减少缓存失效。

b. MC（Multi-Core）层级

• 范围：单个CCX内的所有核心（如8个核心）。

• 关键特性：

  • 共享L3缓存，核心间延迟极低（~10ns）。

  • 调度器优先在同一个CCX内迁移任务，保持数据局部性。

c. NUMA层级（最高层）

• 范围：跨CCD或跨CPU封装（多个NUMA节点）。

• 优化策略：

  • 若任务需要跨CCD调度，优先选择本地NUMA节点内的CCD。

  • 通过numactl或内核参数（如kernel.sched_domain.cpuX.domainN_flags）绑定任务以减少Infinity Fabric延迟。

3. 关键调度参数与优化

• sched_mc_power_savings：

  • 控制是否优先在同一个CCX内调度以降低功耗（对移动端Zen尤其重要）。

• sched_numa_balancing：

  • 启用NUMA感知的任务迁移，减少跨CCD内存访问（Zen 3+默认更激进）。

• cache_level属性：

  • 内核通过CPUID和ACPI SLIT表识别CCX边界，确保调度器感知L3共享域。

4. 用户态工具验证

• lstopo：可视化CCX和NUMA拓扑。
  lstopo --of png > zen3_topology.png
  
• taskset/numactl：手动绑定任务到CCX或NUMA节点：
  numactl --cpubind=0-7 --membind=0 ./program  # 绑定到第一个CCX
  

5. 性能影响示例

• 跨CCX延迟：Zen 3中，同一CCX内核心间延迟约10ns，跨CCX延迟可能翻倍。

• 基准测试建议：

  • 禁用NUMA平衡后测试跨CCX调度开销：
    echo 0 > /proc/sys/kernel/sched_numa_balancing
    

总结

AMD Zen的调度域划分以CCX为关键边界，Linux内核通过三级调度域（SMT→MC→NUMA）实现缓存感知和NUMA优化。用户需结合硬件拓扑调整调度策略，尤其在高并发或低延迟场景下显式绑定任务到CCX可显著提升性能。