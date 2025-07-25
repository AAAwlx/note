# 能量模型与EAS

## 能量模型中的结构

### perf_domain

​​性能域（perf_domain）​​ 是 Linux 内核调度器 EAS 中的一个概念，用于描述​​一组具有相同性能特性的 CPU​​。同时对于这样一组 CPU 都有自己对应的 cpufreq policy。

```c
/* 性能域结构体 */
struct perf_domain {
	struct em_perf_domain *em_pd; /* 能源模型性能域指针 */
	struct perf_domain *next;     /* 指向下一个性能域的指针 */
	struct rcu_head rcu;          /* RCU 头，用于延迟销毁 */
};
```

如果一个平台有 ​​4+3+1​​ 的 CPU 架构（如 4 个大核、3 个中核、1 个小核），那么：

​​大核（4 个）​​ → 1 个 cpufreq policy→ 1 个 perf_domain
​​中核（3 个）​​ → 1 个 cpufreq policy→ 1 个 perf_domain
​​小核（1 个）​​ → 1 个 cpufreq policy→ 1 个 perf_domain

因此，整个系统会有 ​​3 个 perf_domain​​，它们以链表形式组织，链表头存储在 ​​root domain​​ 中。

### em_perf_domain

在EM framework中，我们使用em_perf_domain来抽象一个performance domain：

```c
struct em_perf_domain {
	struct em_perf_table __rcu *em_table;// 指向运行时可修改的 em_perf_table 的指针
	int nr_perf_states;// 性能状态的数量
	int min_perf_state;// 允许的最小性能状态索引
	int max_perf_state;// 允许的最大性能状态索引
	unsigned long flags;
	unsigned long cpus[];
};
```
```c
/**
 * struct em_perf_table - 性能状态表
 * @rcu:	用于安全访问和销毁的 RCU
 * @kref:	用于跟踪用户的引用计数器
 * @state:	按升序排列的性能状态列表
 */
struct em_perf_table {
	struct rcu_head rcu; // RCU 用于在并发环境中安全地更新和销毁数据
	struct kref kref;    // 引用计数器，用于管理对象的生命周期
	struct em_perf_state state[]; // 性能状态数组，按升序排列
};
```

### struct em_perf_state

每个性能域都有若干个perf level，每一个perf level对应能耗是不同的，Linux 中使用 struct em_perf_state 来表示一个perf level下的能耗信息。

```c
struct em_perf_state {
	unsigned long performance; // 在给定频率下的 CPU 性能（容量）
	unsigned long frequency;   // 以 KHz 为单位的频率，与 CPUFreq 保持一致
	unsigned long power;       // 在此级别消耗的功率（由 1 个 CPU 或注册设备消耗）。它可以是总功率：静态和动态功率的总和。
	unsigned long cost;        // 成本系数,与此级别相关的成本系数，用于能量计算。
	unsigned long flags;       // 标志位
};
```

## 能量计算

对能量最基本的计算公式是：能量 = 功率 * 时间

对于 CPU 来说这里可以细化为：CPU的能量消耗 = CPU运行频率对应的功率 x  CPU在该频点运行时间

### 公式 (1): 性能估算

```c
ps->performance = (ps->freq * scale_cpu) / cpu_max_freq      (1)
```

**含义：**

* `ps->performance` 表示当前 CPU 在某个 performance state（性能状态，比如 DVFS 下的频率等级）下的性能值。
* `ps->freq` 是当前频率，单位 MHz。
* `scale_cpu` 是该 CPU 的最大计算能力（capacity scale，例如 1024）。
* `cpu_max_freq` 是该 CPU 的最高频率。

本质上，这个公式计算的是 CPU 当前性能状态相对于最大性能状态的能力比值。

### 公式 (2): CPU 当前能耗估算

```c
cpu_nrg = (ps->power * cpu_util) / ps->performance           (2)
```

**含义：**

* `ps->power` 是该性能状态下的 **功耗（Watts）**，从 Energy Model (EM) 中来。
* `cpu_util` 是该 CPU 当前的负载，通常为 0\~1024 范围的一个值。
* `ps->performance` 是当前性能状态对应的性能（来自公式1）。

理解方式：

`cpu_util / ps->performance` 表示 **这个 CPU 正在工作（busy）的比例**。然后乘以功率 `ps->power`，就得到了单位调度周期下的能耗。

注意：虽然单位仍然是功率（Watts），但在短时间内是近似恒定的，可以看作能量（Energy）。

### 公式 (3): 代入公式1，优化表达

```c
cpu_nrg = (ps->power * cpu_max_freq) / (ps->freq * scale_cpu) * cpu_util   (3)
```

这里把 (1) 带入了 (2)，得到一个新形式。

优点：

* 拆分成两个部分：

  * 静态的部分（即：与 util 无关的）：

    ```c
    ps->cost = (ps->power * cpu_max_freq) / (ps->freq * scale_cpu)
    ```

  * 动态的部分：

    ```c
    cpu_util
    ```

于是：

```c
cpu_nrg = ps->cost * cpu_util
```

## 公式 (4): 计算整个调度域（perf domain）的总能耗

```c
pd_nrg = ps->cost * \Sum cpu_util                         (4)
```

**含义：**

* 一个调度域中的多个 CPU（比如一个 cluster，大小核）共用相同的 `ps->cost`，因为架构一样。
* 所以总能耗就只需要 **对负载求和** 再乘上 cost。

在能耗感知调度中，调度器需要在多个 CPU 中选择负载放在哪个核上。这个模型帮助内核评估：

* 如果将任务调度到某个 CPU，会带来多大的能耗；
* 从而做出 **性能和能耗的平衡决策**（比如偏向小核以省电）。

### 能量计算的代码
该函数主要用来计算当任务p放置到dst_cpu的时候，pd所在的perf domain会消耗多少能量。任务p放置到dst cpu的时候，这会影响pd所在的perf domain中各个CPU的utility，进而会驱动cpufreq进行频率调整。计算能量的时候，我们必须预测可能的调频，并通过这些utility来预估计任务放置在dst cpu之后该perf domain的能耗。
```c
static inline unsigned long
compute_energy(struct energy_env *eenv, struct perf_domain *pd,
	       struct cpumask *pd_cpus, struct task_struct *p, int dst_cpu)
{
	unsigned long max_util = eenv_pd_max_util(eenv, pd_cpus, p, dst_cpu);
	unsigned long busy_time = eenv->pd_busy_time;
	unsigned long energy;

	if (dst_cpu >= 0)
		busy_time = min(eenv->pd_cap, busy_time + eenv->task_busy_time);

	energy = em_cpu_energy(pd->em_pd, max_util, busy_time, eenv->cpu_cap);

	trace_sched_compute_energy_tp(p, dst_cpu, energy, max_util, busy_time);

	return energy;
}
```
这里首先会调用 eenv_pd_max_util 函数，在该函数中会遍历该性能域中的所有 CPU 找出最大的有效 CPU 利用率。
在该函数中会调用：
1. cpu_util 函数计算当前 CPU 的利用率（util）。如果当前 CPU 是目标 CPU（dst_cpu），则将任务 p 视为正在运行的任务。利用率的计算可能会考虑任务的负载、CPU 的当前状态等。
2. effective_cpu_util 函数，结合 CPU 的利用率和频率限制（如 uclamp），计算出 CPU 的有效利用率（eff_util）。uclamp 是一种机制，用于对任务的利用率施加上下限约束（UCLAMP_MIN 和 UCLAMP_MAX）。

然后会调用 em_cpu_energy 函数对放置任务后需要消耗的能量值进行估算。在该函数中会根据上面的公式与 max_util 来对放置该任务后所需要的能量来进行估算。上面公式中的 Sum cpu_util 带入的就是 max_util 。

```c
/*
 * 计算性能域(performance domain)的能量消耗
 * @pd: 性能域的能效模型(Energy Model)指针
 * @max_util: 性能域中最繁忙CPU的利用率
 * @sum_util: 性能域中所有CPU利用率的和
 * @allowed_cpu_cap: 允许的CPU最大容量(可能因热限制而降低)
 * 返回值: 估算的性能域总能量消耗
 */
static inline unsigned long em_cpu_energy(struct em_perf_domain *pd,
				unsigned long max_util, unsigned long sum_util,
				unsigned long allowed_cpu_cap)
{
	struct em_perf_table *em_table;
	struct em_perf_state *ps;
	int i;

#ifdef CONFIG_SCHED_DEBUG
	/* 调试模式下检查RCU读锁是否已持有 */
	WARN_ONCE(!rcu_read_lock_held(), "EM: rcu read lock needed\n");
#endif

	/* 如果没有利用率，直接返回0能量消耗 */
	if (!sum_util)
		return 0;

	/*
	 * 将最繁忙CPU的利用率限制到允许的CPU容量
	 * 这是为了考虑热限制等导致的性能降低
	 */
	max_util = min(max_util, allowed_cpu_cap);

	/*
	 * 在能效模型中查找满足max_util要求的最低性能状态
	 * em_table包含性能状态表
	 */
	em_table = rcu_dereference(pd->em_table);
	i = em_pd_get_efficient_state(em_table->state, pd, max_util);
	ps = &em_table->state[i];  // 获取对应的性能状态

	/*
	 * 计算并返回总能量消耗:
	 * 能量 = 性能状态成本系数 * 总利用率
	 * 
	 * 其中性能状态成本系数(ps->cost)是预先计算的:
	 * cost = (power * cpu_max_freq) / (freq * scale_cpu)
	 * 
	 * 这个公式的推导:
	 * 1. 首先计算性能状态下的CPU性能(容量):
	 *    performance = (ps->freq * scale_cpu) / cpu_max_freq
	 * 2. 然后计算单个CPU的能量:
	 *    cpu_nrg = (ps->power * cpu_util) / performance
	 * 3. 将1代入2后可以简化为:
	 *    cpu_nrg = ps->cost * cpu_util
	 * 4. 整个性能域的能量就是所有CPU能量之和:
	 *    pd_nrg = ps->cost * sum_util
	 */
	return ps->cost * sum_util;
}
```

## EAS

异构CPU拓扑架构（比如Arm big.LITTLE架构），存在高性能的cluster和低功耗的cluster，它们的算力（capacity）之间存在差异，这让调度器在唤醒场景下进行task placement变得更加复杂，我们希望在不影响整体系统吞吐量的同时，尽可能地节省能量，因此，EAS应运而生。它的设计令调度器在做CPU选择时，增加能量评估的维度，它的运作依赖于上述的 EM 模型。

EAS对能量（energy）和功率（power）的定义与传统意义并无差别，energy是类似电源设备上的电池这样的资源，单位是焦耳，power则是每秒的能量损耗值，单位是瓦特。

EAS选核的策略：

主要策略：

1. 优先选择目标任务所在 cluster：任务会尽量被安排在一个 cluster 内（如 big 核簇或 little 核簇）。目的是避免跨 cluster 唤醒，降低唤醒能耗和切换开销。

2. cluster 内按能耗模型选择最优 CPU。在 cluster 内，遍历候选 CPU，使用 Energy Model（EM）计算每个可能调度方案下的总能耗。选择使得系统能耗总和最小的 CPU。

3. 避免将任务堆叠到单核，适度分散。虽然任务全堆到一个 CPU 上能让其他核 idle，更省电，但会使该核频率提升，且 cluster 更难整体 idle。因此在选定 cluster 后，尽量在 cluster 内把任务“适度”均摊分散。

4. EAS 只在异构（非对称）系统开启。只有在 SD_ASYM_CPUCAPACITY 标志存在时才启用。因为只有 big.LITTLE 这类架构下，不同核心能耗差异显著，EAS 才能发挥作用。

同时对于fork出的任务，因为这里并没有util的数据，因此难以预测任务需要消耗的能量。所以针对对fork出来的任务仍然使用慢速路径（find_idlest_cpu函数）找到最空闲的group/cpu并将其放置在该cpu上执行。

```c
static int find_energy_efficient_cpu(struct task_struct *p, int prev_cpu)
{
    struct cpumask *cpus = this_cpu_cpumask_var_ptr(select_rq_mask);  // 获取当前 CPU 对应的 cpumask
    unsigned long prev_delta = ULONG_MAX, best_delta = ULONG_MAX;  // 初始化能耗差值，表示最差情况
    unsigned long p_util_min = uclamp_is_used() ? uclamp_eff_value(p, UCLAMP_MIN) : 0;  // 获取任务 p 的最小 uclamp 值（如果启用的话）
    unsigned long p_util_max = uclamp_is_used() ? uclamp_eff_value(p, UCLAMP_MAX) : 1024;  // 获取任务 p 的最大 uclamp 值
    struct root_domain *rd = this_rq()->rd;  // 获取当前 root domain
    int cpu, best_energy_cpu, target = -1;  // 初始化目标 CPU
    int prev_fits = -1, best_fits = -1;  // 记录 prev_cpu 和最佳 CPU 的匹配度
    unsigned long best_thermal_cap = 0;  // 初始化最佳热容量
    unsigned long prev_thermal_cap = 0;  // 初始化 prev_cpu 的热容量
    struct sched_domain *sd;  // 调度域
    struct perf_domain *pd;  // 性能域
    struct energy_env eenv;  // 能量环境，保存 CPU 和性能域的相关信息

    rcu_read_lock();  // 获取 RCU 锁，确保在多线程环境下读取安全

    pd = rcu_dereference(rd->pd);  // 获取 root domain 的性能域链表
    if (!pd || READ_ONCE(rd->overutilized))  // 如果没有性能域或者 root domain 已超负荷，直接退出
        goto unlock;

    // 1.获取 sd_asym_cpucapacity（异构 CPU 的调度域），该调度域表示具有不同 CPU 性能的 cluster
    sd = rcu_dereference(*this_cpu_ptr(&sd_asym_cpucapacity));
    while (sd && !cpumask_test_cpu(prev_cpu, sched_domain_span(sd)))  // 在 sd 中查找 prev_cpu
        sd = sd->parent;  // 如果 prev_cpu 不在当前 sd 的范围内，则查找父级调度域
    if (!sd)  // 如果没有找到有效的调度域，退出
        goto unlock;

    target = prev_cpu;  // 默认将 prev_cpu 作为目标 CPU

    sync_entity_load_avg(&p->se);  // 2.同步任务 p 的负载平均值

    // 如果任务没有负载或最小 uclamp 值为 0，则直接退出
    if (!task_util_est(p) && p_util_min == 0)
        goto unlock;

    eenv_task_busy_time(&eenv, p, prev_cpu);  // 计算任务在 prev_cpu 上的忙碌时间

    // 遍历当前 root domain 中的每个性能域
    for (; pd; pd = pd->next) {
        unsigned long util_min = p_util_min, util_max = p_util_max;  // 任务的最小和最大利用率
        unsigned long cpu_cap, cpu_thermal_cap, util;  // CPU 的容量、热容量和利用率
        long prev_spare_cap = -1, max_spare_cap = -1;  // 记录 CPU 剩余的计算能力
        unsigned long rq_util_min, rq_util_max;  // 调度队列的最小和最大利用率
        unsigned long cur_delta, base_energy;  // 当前 CPU 的能耗差异和基础能耗
        int max_spare_cap_cpu = -1;  // 记录剩余计算能力最多的 CPU
        int fits, max_fits = -1;  // 记录任务是否适配该 CPU 的能力

        cpumask_and(cpus, perf_domain_span(pd), cpu_online_mask);  // 获取性能域内在线的 CPU
        if (cpumask_empty(cpus))  // 如果没有在线 CPU，则跳过该性能域
            continue;

        // A.计算当前 CPU 的热容量，考虑 thermal pressure（热压力）
        cpu = cpumask_first(cpus);
        cpu_thermal_cap = arch_scale_cpu_capacity(cpu);
        cpu_thermal_cap -= arch_scale_thermal_pressure(cpu);  // 减去 thermal pressure 对容量的影响

        eenv.cpu_cap = cpu_thermal_cap;  // 将热容量赋给能量环境
        eenv.pd_cap = 0;  // 初始化性能域容量

        // 遍历性能域内的每个 CPU，计算它们的负载情况
        for_each_cpu(cpu, cpus) {
            struct rq *rq = cpu_rq(cpu);  // 获取当前 CPU 的调度队列

            eenv.pd_cap += cpu_thermal_cap;  // 累加性能域的热容量

            if (!cpumask_test_cpu(cpu, sched_domain_span(sd)))  // 如果 CPU 不在调度域范围内，则跳过
                continue;

            if (!cpumask_test_cpu(cpu, p->cpus_ptr))  // 如果该 CPU 不在任务的可用 CPU 范围内，则跳过
                continue;

            util = cpu_util(cpu, p, cpu, 0);  // 获取当前 CPU 的负载利用率，cfs_rq->avg.util_avg
            cpu_cap = capacity_of(cpu);  // 获取 CPU 的计算能力

            // 如果 CPU 无法满足任务的负载需求，则跳过
            if (uclamp_is_used() && !uclamp_rq_is_idle(rq)) {
                // 对 uclamp 进行处理，获取任务在该 CPU 上的最大利用率
                rq_util_min = uclamp_rq_get(rq, UCLAMP_MIN);
                rq_util_max = uclamp_rq_get(rq, UCLAMP_MAX);

                util_min = max(rq_util_min, p_util_min);
                util_max = max(rq_util_max, p_util_max);
            }

            fits = util_fits_cpu(util, util_min, util_max, cpu);  // 判断该 CPU 是否能满足任务的需求
            if (!fits)  // 如果不能适配，则跳过该 CPU
                continue;

            lsub_positive(&cpu_cap, util);  // 减去任务占用的 CPU 能力

            // 如果是 prev_cpu，则将它作为候选 CPU
            if (cpu == prev_cpu) {
                prev_spare_cap = cpu_cap;
                prev_fits = fits;
            } else if ((fits > max_fits) || ((fits == max_fits) && ((long)cpu_cap > max_spare_cap))) {
                // 寻找具有最大剩余计算能力的 CPU
                max_spare_cap = cpu_cap;
                max_spare_cap_cpu = cpu;
                max_fits = fits;
            }
        }

        // 如果没有找到合适的 CPU，则跳过
        if (max_spare_cap_cpu < 0 && prev_spare_cap < 0)
            continue;

        eenv_pd_busy_time(&eenv, cpus, p);  // 计算性能域内任务的忙碌时间
        base_energy = compute_energy(&eenv, pd, cpus, p, -1);  // 计算不包含任务的性能域基础能耗

        // 评估使用 prev_cpu 的能量影响
        if (prev_spare_cap > -1) {
            prev_delta = compute_energy(&eenv, pd, cpus, p, prev_cpu);
            if (prev_delta < base_energy)  // 如果任务放在 prev_cpu 上的能耗更低，直接返回
                goto unlock;
            prev_delta -= base_energy;
            prev_thermal_cap = cpu_thermal_cap;
            best_delta = min(best_delta, prev_delta);
        }

        // 评估使用 max_spare_cap_cpu 的能量影响
        if (max_spare_cap_cpu >= 0 && max_spare_cap > prev_spare_cap) {
            if (max_fits < best_fits)
                continue;

            if ((max_fits < 0) && (cpu_thermal_cap <= best_thermal_cap))
                continue;

            cur_delta = compute_energy(&eenv, pd, cpus, p, max_spare_cap_cpu);
            if (cur_delta < base_energy)
                goto unlock;
            cur_delta -= base_energy;

            if ((max_fits > 0) && (best_fits > 0) && (cur_delta >= best_delta))
                continue;

            best_delta = cur_delta;
            best_energy_cpu = max_spare_cap_cpu;
            best_fits = max_fits;
            best_thermal_cap = cpu_thermal_cap;
        }
    }
    rcu_read_unlock();  // 解锁 RCPU

    // 根据能耗比较选择最优的目标 CPU
    if ((best_fits > prev_fits) ||
        ((best_fits > 0) && (best_delta < prev_delta)) ||
        ((best_fits < 0) && (best_thermal_cap > prev_thermal_cap)))
        target = best_energy_cpu;

    return target;  // 返回最节能的 CPU

unlock:
    rcu_read_unlock();
    return target;  // 在出错时返回当前目标 CPU
}
```

在上述代码中会经过如下的过程

**1.确定一个sched domain**

自下而上的遍历性能域，通过 sd_asym_cpucapacity 获取当前 CPU 所在的性能域，并且根据该性能域是否包含 prev_cpu，决定是否进入下一个性能域。

prev_cpu 表示的是任务上次运行时所在的 CPU。它是一个指向当前进程 先前所在 CPU 的标识符，即该任务在被唤醒之前的执行 CPU。

为什么需要 prev_cpu？

* 任务亲和性：在多数情况下，任务在同一个 CPU 上运行时，能够利用 CPU 缓存，因为它可能之前的操作还没有完全被清除，从而减少缓存缺失（cache miss）和提高性能。

* 调度优化：通过将任务恢复到其 上次执行的 CPU（即 prev_cpu），可以减少 CPU 之间的迁移带来的开销。如果没有特别的负载均衡或能效考虑，优先将任务调度到 prev_cpu 是一种优化。

**2. 更新负载**

因为后面需要任务p的utility参与计算（估计调频点和能耗），而该任务在被唤醒之前应该已经阻塞了一段时间，它的utility还停留在阻塞前那一点，因此这里需要按照等待的这一段时间进行负载衰减。

这样做是因为在任务等待期间（例如 I/O 操作、信号等）并未消耗 CPU 资源，但它的负载可能会保持在较高的水平，直到被唤醒。若不进行负载衰减，任务被唤醒后 可能会拥有一个过高的负载值，导致调度器错误地认为它需要更多的计算资源。

假设任务 p 在阻塞时的负载值是 较高 的（比如它在等待磁盘 I/O 时已经准备好很大的负载）。如果唤醒后 不衰减负载，调度器可能会认为它需要更多的计算能力，可能会调度到 性能更高的 CPU，造成不必要的 能耗增加。

**3. EAS核心代码**
在 for 循环中执行了 EAS 最核心的代码，在该循环会遍历所有的性能域。

A.获取 CPU 的 ​​实际可用计算能力。

B、遍历该pd的各个cpu，计算各个cpu的剩余算力。这里的剩余算力是这样计算的：获取该cpu用于cfs task的算力（即原始算力去掉rt irq等消耗的算力），减去该cpu的util（即cfs task的总的utility）。这里的cpu util是把任务p放置在该cpu的预估utility（cpu_util_next）。

```c
lsub_positive(&cpu_cap, util);  // 剩余容量 = CPU 容量 - 当前负载
if (cpu == prev_cpu) {
    prev_spare_cap = cpu_cap;  // 记录 prev_cpu 的剩余容量
} else if ((fits > max_fits) || (fits == max_fits && cpu_cap > max_spare_cap)) {
    max_spare_cap = cpu_cap;   // 记录最大剩余容量的 CPU
    max_spare_cap_cpu = cpu;
}
```

能耗评估：

```c
base_energy = compute_energy(&eenv, pd, cpus, p, -1);  // 不放置任务时的能耗
prev_delta = compute_energy(&eenv, pd, cpus, p, prev_cpu) - base_energy;  // 放置到 prev_cpu 的能耗增量
cur_delta = compute_energy(&eenv, pd, cpus, p, max_spare_cap_cpu) - base_energy;  // 放置到新 CPU 的能耗增量

if (prev_delta < base_energy) goto unlock;  // prev_cpu 更优
if (cur_delta < best_delta) {
    best_energy_cpu = max_spare_cap_cpu;  // 选择能耗更低的 CPU
}
```

在这一部分中，代码分别计算了使用 `prev_cpu` 和 `max_spare_cap_cpu` 的能耗，并通过对比这两者的能耗差异，决定最终选择哪个 CPU。

如果将任务放置在 prev_cpu 上的 能耗 低于系统的基础能耗 (base_energy)，并且能耗差异 (prev_delta) 较小，调度器就会直接选择 prev_cpu 作为目标。

如果 prev_cpu 的能耗较高，调度器会继续寻找其他 CPU 来调度任务，最终选择 最小化能耗的 CPU。

这里最终会调用到函数 em_cpu_energy 根据任务的负载利用率、CPU 的忙碌时间 和 CPU 的计算能力 来计算任务在该 CPU 上的能耗。

能耗差异的意义：通过计算能耗差异，调度器能够评估任务在不同 CPU 上运行时对能耗的影响，从而选择 能耗最低 的 CPU 来调度任务。

最终目标：通过比较不同 CPU 的能耗差异，选择最适合放置任务的 CPU，从而 优化系统能效，降低不必要的能耗。

这段代码通过​三层筛选​​完成选核：
* ​性能域层​​：选择任务允许运行的 CPU 集群。
* ​​容量层​​：筛选剩余计算能力足够的 CPU。
* 能耗层​​：选择能耗增量最小的 CPU。
