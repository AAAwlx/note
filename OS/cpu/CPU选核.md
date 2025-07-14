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

find_idlest_cpu()，即慢速路径。有两种常见的情况会进入慢速路径：传入参数sd_flag为SD_BALANCE_WAKE，且EAS没有使能或者返回-1时，如果该任务不是wake affine类型，就会进入慢速路径；传入参数sd_flag为SD_BALANCE_FORK、SD_BALANCE_EXEC时，由于此时的任务负载是不可信任的，无法预测其对系统能耗的影响，也会进入慢速路径。慢速路径使用find_idlest_cpu()方法找到系统中最空闲的CPU，作为放置任务的CPU并返回。基本的搜索流程是：

首先确定放置的target domain（从waker的base domain向上，找到最底层配置相应sd_flag的domain），然后从target domain中找到负载最小的调度组，进而在调度组中找到负载最小的CPU。

这种选核方式对于刚创建的任务来说，算是一种相对稳妥的做法，开发者也指出，或许可以将新创建的任务放置到特殊类型的CPU上，或者通过它的父进程来推断它的负载走向，但这些启发式的方法也有可能在一些使用场景下造成其他问题。

#### 快速路径

（3）select_idle_sibling()，即快速路径。传入参数sd_flag为SD_BALANCE_WAKE，但EAS又无法发挥作用时，若该任务为wake affine类型任务，调度器就会进入快速路径来选取放置的CPU，该路径在CPU的选择上，主要考虑共享cache且idle的CPU。在满足条件的情况下，优先选择任务上一次运行的CPU（prev cpu），hot cache的CPU是wake affine类型任务所青睐的。其次是唤醒任务的CPU（wake cpu），即waker所在的CPU。当该次唤醒为sync唤醒时（传入参数wake_flags为WF_SYNC），对wake cpu的idle状态判定将会放宽，比如waker为wake cpu唯一的任务，由于sync唤醒下的waker很快就进入阻塞状态，也可当做idle处理。

如果prev cpu或者wake cpu无法满足条件，那么调度器会尝试从它们的LLC domain中去搜索idle的CPU。

## EAS

