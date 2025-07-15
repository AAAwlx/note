# CPU 选核

在 CPU 运行任务时，会使用 CPU rq 来记录当前 CPU 上所有需要运行的任务。调度器会适时地通过负载均衡（load balance）来调整任务的分布；当它从runqueue中取出并开始执行时，便处于运行状态，若该状态下的任务负载不是当前CPU所能承受的，那么调度器会将其标记为misfit task，周期性地触发主动迁移（active upmigration）。

有三种情况需要为task选择cpu：

1. 刚创建的进程（fork）
2. 刚exec的进程（exec）
3. 刚被唤醒的阻塞的进程（wakeup）

最终这三个函数都会调到 select_task_rq -> select_task_rq_fair。

![Alt text](cpu选核函数调用.png)

## select_task_rq_fair

```c
static int
select_task_rq_fair(struct task_struct *p, int prev_cpu, int wake_flags)
{
	// 判断是否是同步唤醒（WF_SYNC）且当前进程不是正在退出的任务
	int sync = (wake_flags & WF_SYNC) && !(current->flags & PF_EXITING);
	struct sched_domain *tmp, *sd = NULL;

	// 当前 CPU ID
	int cpu = smp_processor_id();
	// 初始默认目标 CPU 是唤醒前所在 CPU
	int new_cpu = prev_cpu;

	// 是否希望使用 wake_affine（即：唤醒尽量在唤醒者本地 CPU）
	int want_affine = 0;

	// 提取 wake_flags 的低 4 位，作为调度域的标志（SD_BALANCE_WAKE / SD_BALANCE_EXEC 等）
	int sd_flag = wake_flags & 0xF;

	// 断言任务 p 的 pi_lock 已持有，保证 cpus_ptr 是一致可见的
	lockdep_assert_held(&p->pi_lock);

	// 如果是 try-to-wake-up（TTWU）路径
	if (wake_flags & WF_TTWU) {
		// 记录唤醒者-被唤醒者的关系，用于唤醒跟踪/调优
		record_wakee(p);

		// 如果当前 CPU 在任务允许的亲和性掩码中，并且设置了 WF_CURRENT_CPU，则直接返回当前 CPU
		if ((wake_flags & WF_CURRENT_CPU) &&
		    cpumask_test_cpu(cpu, p->cpus_ptr))
			return cpu;

		// 如果启用了 EAS（能耗感知调度）
		if (sched_energy_enabled()) {
			// 尝试选择能耗最优的 CPU
			new_cpu = find_energy_efficient_cpu(p, prev_cpu);
			if (new_cpu >= 0)
				return new_cpu;

			// 没有找到合适的能效 CPU，回退使用 prev_cpu
			new_cpu = prev_cpu;
		}

		// 判断是否使用 wake_affine：
		// - 唤醒者不是宽任务（wake_wide 为 false）
		// - 当前 CPU 在任务的亲和性掩码中
		want_affine = !wake_wide(p) && cpumask_test_cpu(cpu, p->cpus_ptr);
	}

	// 进入 RCU 读保护区，遍历调度域
	rcu_read_lock();
	for_each_domain(cpu, tmp) {
		// 如果 prev_cpu 与当前 CPU 在同一个调度域，并设置了 SD_WAKE_AFFINE，
		// 则认为可以进行 wake_affine 策略选择
		if (want_affine && (tmp->flags & SD_WAKE_AFFINE) &&
		    cpumask_test_cpu(prev_cpu, sched_domain_span(tmp))) {
			// 如果当前 CPU 与 prev_cpu 不一致，调用 wake_affine 进行亲和性选择
			if (cpu != prev_cpu)
				new_cpu = wake_affine(tmp, p, cpu, prev_cpu, sync);

			sd = NULL; // 不再走慢路径（负载均衡），优先使用 wake_affine
			break;
		}

		// 慢路径：当前调度域支持 sd_flag（如 SD_BALANCE_WAKE）则暂存该调度域
		if (tmp->flags & sd_flag)
			sd = tmp;
		else if (!want_affine)
			// 如果不希望使用亲和性策略，遇到不支持 sd_flag 的调度域就退出
			break;
	}

	// 如果找到支持 sd_flag 的调度域（慢路径）
	if (unlikely(sd)) {
		// 在该调度域中选择空闲或最轻负载的 CPU
		new_cpu = find_idlest_cpu(sd, p, cpu, prev_cpu, sd_flag);
	}
	// 否则，如果是 TTWU 路径（快速路径）
	else if (wake_flags & WF_TTWU) {
		// 从 prev_cpu 和 new_cpu 中尝试找一个空闲 CPU
		new_cpu = select_idle_sibling(p, prev_cpu, new_cpu);
	}

	// 退出 RCU 读保护区
	rcu_read_unlock();

	// 返回最终选择的 CPU
	return new_cpu;
}
```

### flag标志状态

| 宏名               | 含义                       | 用途/场景                |
| ---------------- | ------------------------ | -------------------- |
| `WF_EXEC`        | exec() 后唤醒               | 映射 SD\_BALANCE\_EXEC |
| `WF_FORK`        | fork() 后唤醒               | 映射 SD\_BALANCE\_FORK |
| `WF_TTWU`        | 普通的 try\_to\_wake\_up 唤醒 | 映射 SD\_BALANCE\_WAKE |
| `WF_SYNC`        | 唤醒者唤醒后会睡眠                | 有利于亲和唤醒              |
| `WF_MIGRATED`    | 任务已在唤醒流程中被迁移             | 内部使用                 |
| `WF_CURRENT_CPU` | 优先将唤醒任务放到当前 CPU          | 实现 CPU-local 的唤醒     |

在这个函数中实际上指挥使用到前三个状态。

sched_domain 标志，用于表示某个调度域 (sched_domain) 的行为特性和调度策略。这些标志被用于控制调度器如何跨 CPU 或 CPU 组进行负载均衡、唤醒决策、能耗感知调度（EAS）等。

| 标志                         | 说明                                             |
| -------------------------- | ---------------------------------------------- |
| `SD_BALANCE_NEWIDLE`       | 当前 CPU 空闲时尝试从其他 CPU 拉取任务。用于 *newidle balance*。 |
| `SD_BALANCE_EXEC`          | `exec()` 后在更空闲的 CPU 上启动程序。用于平衡进程执行启动负载。        |
| `SD_BALANCE_FORK`          | 子进程创建（`fork()`）后可能被迁移到其他 CPU 执行。               |
| `SD_BALANCE_WAKE`          | 唤醒任务时尝试迁移到其他更合适 CPU（通常是空闲的）。                   |
| `SD_WAKE_AFFINE`           | 尝试将被唤醒任务唤醒到 waker 所在 CPU 上，提升 cache 命中率。       |
| `SD_ASYM_CPUCAPACITY`      | 调度域内 CPU 计算能力不一致（如 big.LITTLE 架构）。             |
| `SD_ASYM_CPUCAPACITY_FULL` | 调度域完整覆盖了所有不同的 CPU 能力等级。                        |
| `SD_SHARE_CPUCAPACITY`     | 调度域内 CPU 共用能力资源（如 SMT 超线程）。                    |
| `SD_CLUSTER`               | 调度域是一个 CPU 集群，如共享 L2 cache 的核心组。               |
| `SD_SHARE_LLC`             | 调度域成员共享 Last Level Cache（LLC），如共享 L3 cache 的核。 |
| `SD_SERIALIZE`             | 序列化调度，避免多个任务同时从这个域做负载均衡。                       |
| `SD_ASYM_PACKING`          | 将繁忙任务尽可能集中在高性能核（优先填满）。                         |
| `SD_PREFER_SIBLING`        | 在任务调度时优先选择邻近的兄弟域。                              |
| `SD_OVERLAP`               | sched\_group 存在重叠情况（比如 numa 节点间 CPU 组重叠）。      |
| `SD_NUMA`                  | 该域支持跨 NUMA 节点调度任务。                             |

上述这些标志会影响到后续的选核方式。

### 选核

select_task_rq_fair()中涉及到三个重要的选核函数：

* find_energy_efficient_cpu()
* find_idlest_cpu()
* select_idle_sibling()

它们分别代表任务放置过程中的三条路径。task placement的各个场景，根据不同条件，最终都会进入其中某一条路径，得到任务放置CPU并结束此次的task placement过程。

#### find_energy_efficient_cpu EAS 选核路径

find_energy_efficient_cpu()，即EAS选核路径。当传入参数sd_flag为SD_BALANCE_WAKE，并且系统配置key值sched_energy_present（即考虑性能和功耗的均衡），调度器就会进入EAS选核路径进行CPU的查找。EAS路径在保证任务能正常运行的前提下，为任务选取使系统整体能耗最小的CPU。通常情况下，EAS总是能如愿找到符合要求的CPU，但如果当前平台不是异构系统，或者系统中存在超载（Over-utilization）的 CPU，EAS就直接返回-1。

当EAS不能在这次调度中发挥作用时，分支的走向取决于该任务是否为wake affine类型的任务，

Wake affine 的含义是：调度器倾向于将被唤醒的任务（wakee）安排在唤醒它的任务（waker）所在的 CPU 上执行。

当一个任务 A 唤醒任务 B（比如通过 wake_up_process()），这两个任务很可能：

1. 有共享的数据（尤其在进程间通信、管道、mutex、wait queue 等场景）

2. 被调度器希望尽可能地减少跨 CPU 数据迁移成本

此时如果 wakee 继续在 waker 所在的 CPU 上运行，则：可以复用 waker 使用过的 CPU 缓存，避免了跨核迁移带来的上下文切换成本和缓存失效。

但是这个机制还会引起一些问题。

对于一些调度时延敏感的任务来说，如果和唤醒者绑定，那么任务被唤醒后 不能立刻运行，还需要等待唤醒者出让 CPU 的控制权之后才能被调度上 CPU 。这里可能存在有较大的调度时延。

对于wake affine类型的判断，内核主要通过wake_wide()和wake_cap()的实现，从wakee的数量以及临近CPU算力是否满足任务需求这两个维度进行考量。

#### 慢速路径

1. 唤醒类调度（WF_TTWU）且 EAS 不可用或返回 -1，且任务不是 wake affine 类型：

   wake_flags == WF_TTWU 表示这是一个典型的唤醒场景；

   如果系统启用了 EAS（Energy Aware Scheduling）调度器，并且返回了一个合适的能效核，就用它；

   如果 EAS 没使能或找不到合适目标（返回 -1），并且 wakee 不是 wake affine 类型即唤醒者和被唤醒任务关系之间没有关联性，则进入慢速路径。

2. WF_EXEC 或 WF_FORK：
   当任务是由于 exec() 或 fork() 新创建的，其负载模式尚未形成，系统无法预测其运行时行为，不能依赖能耗模型；

   因此采用更保守的策略——从系统范围内选最空闲的 CPU 来放置任务。

在 find_idlest_cpu 从 waker 所在的 base domain 向上查找，找到第一个配置了传入 sd_flag（如 SD_BALANCE_WAKE、SD_BALANCE_EXEC、SD_BALANCE_FORK）的 sched_domain。这就是任务放置的“搜索范围”。

sched_domain 是按层次划分的，每个 domain 包含多个调度组，先比较组之间的平均负载，选一个组。在目标调度组中选择最空闲 CPU，从选中的调度组中挑出一个平均负载最小的 CPU 作为执行者。

#### 快速路径

当进入 select_idle_sibling()的快速路径时，调度器会按以下顺序选择 CPU：

​​(1) 优先选择 prev_cpu（任务上一次运行的 CPU）​​
​
​原因​​：prev_cpu可能仍然缓存了该任务的数据（hot cache），减少 cache miss。

​​条件​​：
1. prev_cpu必须是 ​​idle 状态​。
2. prev_cpu必须和当前 CPU 在同一个 ​​LLC 域​​共享三级缓存。

​​(2) 其次选择 wake_cpu（唤醒者所在的 CPU）​​

​​原因​​：wake_cpu是唤醒者（waker）所在的 CPU，如果任务 B 被任务 A 唤醒，那么 B 在 A 的 CPU 上运行可能更高效（减少数据同步开销）。
​
​条件​​：wake_cpu 必须是 ​​idle 状态​​（或接近 idle）。

​但是也存在​特殊情况如果本次唤醒是 ​​同步唤醒（WF_SYNC）​​，那么即使 wake_cpu 不是完全 idle，也可能被选中。

例如：
wake_cpu上只有一个任务（waker），并且 waker 即将进入阻塞状态（如调用 wait()），那么可以近似认为 wake_cpu是 idle 的。

​​(3) 如果 prev_cpu和 wake_cpu都不满足条件​​

调度器会从 prev_cpu或 wake_cpu的 ​​LLC domain（共享 L3 cache 的 CPU 组）​​ 中搜索一个 idle 的 CPU。

​​目标​​：仍然尽量选择 ​​cache-hot 的 CPU​​，减少 cache miss。

这样做的好处是可以减少 cache miss。让任务在同一个 CPU 或邻近 CPU 上运行时，可以利用​已有的 cache 数据​​，提高性能。



