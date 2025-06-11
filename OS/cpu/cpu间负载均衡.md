# CPU 负载均衡

对于 CPU 的负载均衡大致分为两种

1. 为空闲的 CPU 核从繁忙的 CPU 核中拉取任务执行。
2. 为刚被创建/唤醒的任务选取 CPU 核。

## 触发CPU主动负载均衡的时机

触发负载均衡的路径有三种，分别为周期性负载均衡、NOHZ负载均衡、newidle balance。 这里负载均衡采取的策略都是空闲的 CPU 在各个层级的调度域内从繁忙的 CPU 上主动拉取任务来达到调度域内的平衡。

![alt text](../image/周期性触发负载均衡.png)

### 周期性负载均衡

系统会随着CPU的时钟节拍周期性地触发负载均衡，在每次时钟中断的处理函数中会调用 sched_balance_trigger （原为 trigger_load_balance ）函数来进行负载均衡。

在这里会先判断当前时间已经达到或超过了下一次负载均衡的时间。如果达到了就触发 SCHED_SOFTIRQ 。最终在软中断函数中进行负载均衡处理。

并调用 nohz_balancer_kick 检查是否需要触发 NOHZ 负载均衡。

```c
void sched_balance_trigger(struct rq *rq)
{
	/*
	 * 如果运行队列附加到 NULL 域或运行队列的 CPU 未处于活动状态，
	 * 则不需要进行负载均衡。
	 */
	if (unlikely(on_null_domain(rq) || !cpu_active(cpu_of(rq))))
		return;

	/*
	 * 如果当前时间已经达到或超过了下一次负载均衡的时间，
	 * 则触发 SCHED_SOFTIRQ。
	 */
	if (time_after_eq(jiffies, rq->next_balance))
		raise_softirq(SCHED_SOFTIRQ);

	/*
	 * 检查是否需要触发 NOHZ 负载均衡。
	 */
	nohz_balancer_kick(rq);
}
```

### NOHZ 负载均衡

NOHZ 模式，是 Linux 内核的一项优化功能。传统的 Linux 内核会定期触发定时器中断，即使系统处于空闲状态。这种行为会导致 CPU 无法进入深度休眠状态，从而增加功耗。

NOHZ 模式如果启动的话当CPU在进入 idle 状态就会关闭时钟节拍，使得 CPU 能够进入深度休眠状态，从而降低功耗。只有在需要处理任务时，定时器中断才会重新启用。

但是对于普通的负载均衡来说需要由时钟CPU的时钟节，在 NOHZ 模式下关闭了时钟，那么处于深度休眠的 CPU 又如何能主动的从其他繁忙 CPU 上拉取任务来运行？在这里我们引入了 NOHZ 负载均衡。

对于一个CPU即将进入无时钟(tickless)空闲状态的 CPU 会将 has_blocked 设置为 1 ，以此表明有在 idle 中的 cpu 需要被唤醒执行任务。

```c
void nohz_balance_enter_idle(int cpu)
{
	struct rq *rq = cpu_rq(cpu);

	SCHED_WARN_ON(cpu != smp_processor_id());

	/* If this CPU is going down, then nothing needs to be done: */
	if (!cpu_active(cpu))
		return;

	rq->has_blocked_load = 1;

	if (rq->nohz_tick_stopped)
		goto out;

	if (on_null_domain(rq))
		return;

	rq->nohz_tick_stopped = 1;

	cpumask_set_cpu(cpu, nohz.idle_cpus_mask);
	atomic_inc(&nohz.nr_cpus);

	smp_mb__after_atomic();

	set_cpu_sd_state_idle(cpu);

	WRITE_ONCE(nohz.needs_update, 1);
out:
	WRITE_ONCE(nohz.has_blocked, 1);
}
```

在当前 CPU 进入时钟中断处理函数中后会检查该标志位是否被置位，如果被置位则调用 kick_ilb 函数。

```c
	// 如果需要更新负载均衡状态，则设置 NOHZ_NEXT_KICK 标志
	if (READ_ONCE(nohz.needs_update))
		flags |= NOHZ_NEXT_KICK;

	// 如果有需要触发的标志，则触发 ILB
	if (flags)
		kick_ilb(flags);
```

在 kick_ilb 函数中先会调用 find_new_ilb 找到一个在休眠中的 CPU，并调用 smp_call_function_single_async 在选定的空闲 CPU 上生成一个 IPI 将其唤醒。并且在从 IPI 返回之前会运行执行 NOHZ 空闲负载均衡的软中断。

```c
static void kick_ilb(unsigned int flags)
{
	int ilb_cpu;

	if (flags & NOHZ_BALANCE_KICK)
		nohz.next_balance = jiffies + 1;

	ilb_cpu = find_new_ilb();
	if (ilb_cpu < 0)
		return;

	if ((atomic_read(nohz_flags(ilb_cpu)) & flags) == flags)
		return;

	flags = atomic_fetch_or(flags, nohz_flags(ilb_cpu));

	if (flags & NOHZ_KICK_MASK)
		return;
	smp_call_function_single_async(ilb_cpu, &cpu_rq(ilb_cpu)->nohz_csd);
}
```

在 smp_call_function_single_async 中会回调到 nohz_csd_func 。该函数在 sched_init 时注册。

```c
//sched_init
#ifdef CONFIG_NO_HZ_COMMON
		rq->last_blocked_load_update_tick = jiffies;
		atomic_set(&rq->nohz_flags, 0);

		INIT_CSD(&rq->nohz_csd, nohz_csd_func, rq);
#endif
```

在 nohz_csd_func 中会调用 __raise_softirq_irqoff 函数来触发 SCHED_SOFTIRQ 中断。

### 处理 SCHED_SOFTIRQ 中断

SCHED_SOFTIRQ 软中断的中断处理函数是 sched_balance_softirq 。

```c
static __latent_entropy void sched_balance_softirq(void)
{
	struct rq *this_rq = this_rq();
	enum cpu_idle_type idle = this_rq->idle_balance;

	/*
	 * 如果此 CPU 有待处理的 NOHZ_BALANCE_KICK，则代表其他停止时钟的空闲 CPU
	 * 执行负载均衡。在执行 sched_balance_domains 之前进行 nohz_idle_balance，
	 * 以便空闲 CPU 有机会进行负载均衡。否则，如果我们拉取了一些负载，
	 * 可能只在本地调度域层次结构内进行负载均衡，并完全中止 nohz_idle_balance。
	 */
	if (nohz_idle_balance(this_rq, idle))
		return;

	sched_balance_update_blocked_averages(this_rq->cpu);
	sched_balance_domains(this_rq, idle);
}

```

在这里要先执行 sched_balance_update_blocked_averages 更新​​更新阻塞任务的负载贡献。

因为被阻塞的任务虽然不消耗CPU，但仍会保留在运行队列中，其历史负载会影响调度决策。

若仅根据任务当前状态（阻塞/运行）分配CPU时间可能会导致以下问题：

1. I/O密集型任务唤醒后会突然抢占大量CPU时间
2. CPU密集型任务可能因短期阻塞被不公平地降权

```c
static void sched_balance_update_blocked_averages(int cpu)
{
    // decayed: 标记是否有负载衰减需要处理
    // done: 标记是否所有调度类的更新都已完成
    bool decayed = false, done = true;
    struct rq *rq = cpu_rq(cpu);  // 获取指定CPU的运行队列
    struct rq_flags rf;           // 用于保存中断状态的标志

    /* 加锁并保存中断状态 */
    rq_lock_irqsave(rq, &rf);

    /* 基础更新 */
    update_blocked_load_tick(rq);  // 更新阻塞负载的时钟周期计数
    update_rq_clock(rq);          // 同步运行队列时钟

    /* 更新所有调度类的阻塞负载 */
    // 更新非CFS调度类（如RT、DL）的阻塞负载
    decayed |= __update_blocked_others(rq, &done);
    // 更新CFS调度类的阻塞负载（核心公平调度器）
    decayed |= __update_blocked_fair(rq, &done);

    /* 后续处理 */
    update_blocked_load_status(rq, !done);  // 更新阻塞负载状态标志
    if (decayed)
        cpufreq_update_util(rq, 0);  // 若负载变化，通知CPU频率调节器

    /* 释放锁并恢复中断状态 */
    rq_unlock_irqrestore(rq, &rf);
}
```

在 sched_balance_domains 会遍历当前 CPU 所有的调度域，

```c
/**
 * sched_balance_domains - 在指定CPU的调度域层次结构中执行负载均衡
 * @rq: 目标CPU的运行队列
 * @idle: 标识CPU是否空闲（CPU_IDLE/CPU_NOT_IDLE）
 *
 * 该函数通过遍历CPU的调度域（从最底层到顶层），逐级检查并执行负载均衡。
 * 核心逻辑包括：负载均衡间隔控制、跨域任务迁移、串行化保护等。
 */
static void sched_balance_domains(struct rq *rq, enum cpu_idle_type idle)
{
    int continue_balancing = 1;      // 是否继续向更高层调度域执行均衡
    int cpu = rq->cpu;               // 当前CPU编号
    // 判断CPU是否繁忙：非空闲且非调度器定义的空闲状态
    int busy = idle != CPU_IDLE && !sched_idle_cpu(cpu);
    unsigned long interval;          // 当前调度域的均衡间隔时间
    struct sched_domain *sd;         // 当前遍历的调度域指针
    /* 下次必须执行均衡的最早时间（默认60秒后） */
    unsigned long next_balance = jiffies + 60*HZ;
    int update_next_balance = 0;     // 是否需要更新rq->next_balance
    int need_serialize, need_decay = 0;
    u64 max_cost = 0;                // 记录跨域均衡的最大成本

    /* 开始RCU安全遍历调度域 */
    rcu_read_lock();
    // 从下至上遍历CPU所属的所有调度域（如SMT->MC->NUMA）
    for_each_domain(cpu, sd) {
        /*
         * 衰减newidle状态的最大均衡成本值，因为这是对调度域的常规访问。
         * 返回值表示是否需要衰减rq->max_idle_balance_cost
         */
        need_decay = update_newidle_cost(sd, 0);
        max_cost += sd->max_newidle_lb_cost; // 累加各层均衡成本

        /* 
         * 如果上层调度域已无需均衡：
         * - 若当前域不需要衰减成本，直接退出循环
         * - 否则继续循环以处理成本衰减
         */
        if (!continue_balancing) {
            if (need_decay)
                continue;
            break;
        }

        /* 获取当前调度域的负载均衡间隔（繁忙时用更短间隔） */
        interval = get_sd_balance_interval(sd, busy);

        /* 检查当前域是否需要串行化执行（SD_SERIALIZE标志） */
        need_serialize = sd->flags & SD_SERIALIZE;
        if (need_serialize) {
            // 尝试获取全局均衡锁（防止多CPU同时操作同域）
            if (atomic_cmpxchg_acquire(&sched_balance_running, 0, 1))
                goto out; // 获取锁失败则跳过当前域
        }

        /* 检查是否到达当前调度域的均衡时间点 */
        if (time_after_eq(jiffies, sd->last_balance + interval)) {
            /* 执行实际负载均衡（核心函数） */
            if (sched_balance_rq(cpu, rq, sd, idle, &continue_balancing)) {
                /*
                 * 如果发生了任务迁移：
                 * - 重新检查CPU的空闲状态（LBF_DST_PINNED可能改变目标CPU）
                 * - 更新busy状态标志
                 */
                idle = idle_cpu(cpu);
                busy = !idle && !sched_idle_cpu(cpu);
            }
            sd->last_balance = jiffies; // 记录本次均衡时间
            interval = get_sd_balance_interval(sd, busy); // 重新计算间隔
        }

        /* 释放串行化锁（如果之前获取了） */
        if (need_serialize)
            atomic_set_release(&sched_balance_running, 0);

out:
        /* 更新全局下次均衡时间（取所有域的最早时间） */
        if (time_after(next_balance, sd->last_balance + interval)) {
            next_balance = sd->last_balance + interval;
            update_next_balance = 1;
        }
    }

    /* 处理成本衰减（如果需要） */
    if (need_decay) {
        /*
         * 确保rq->max_idle_balance_cost也衰减，但保持合理下限：
         * - 下限为sysctl_sched_migration_cost（避免rq->avg_idle异常）
         * - 取当前各域max_cost的最大值
         */
        rq->max_idle_balance_cost =
            max((u64)sysctl_sched_migration_cost, max_cost);
    }
    rcu_read_unlock(); // 结束RCU临界区

    /* 
     * 仅在需要时更新next_balance：
     * - 例如当CPU附加到null域时不会更新
     */
    if (likely(update_next_balance))
        rq->next_balance = next_balance;
}
```

### newidle balance

在 cpu 需要进行任务切换时，会选择下一个需要被调度上 CPU 的任务。这里的任务后首先从当前 CPU 的 rq 中选择，如果当前 CPU 的可运行任务队列中已经没有任务可以运行。那么就会触发负载均衡从其他繁忙的 CPU 上来取任务来执行。

>pick_next_task_fair -> sched_balance_newidle

如果拉取到任务，pick_next_task_fair会重新pick task，将拉取到的task调度上去。

```c
static int sched_balance_newidle(struct rq *this_rq, struct rq_flags *rf)
{
	unsigned long next_balance = jiffies + HZ; // 下一次负载均衡的时间
	int this_cpu = this_rq->cpu; // 当前 CPU
	int continue_balancing = 1; // 是否继续负载均衡
	u64 t0, t1, curr_cost = 0; // 记录负载均衡的时间开销
	struct sched_domain *sd; // 调度域
	int pulled_task = 0; // 是否成功拉取任务

	// 更新错配任务状态
	update_misfit_status(NULL, this_rq);

	/*
	 * 如果有任务等待运行，则无需搜索任务。
	 * 返回 0；任务将在切换到空闲状态时入队。
	 */
	if (this_rq->ttwu_pending)
		return 0;

	/*
	 * 必须在调用 sched_balance_rq() 之前设置 idle_stamp，
	 * 以便将此时段计为空闲时间。
	 */
	this_rq->idle_stamp = rq_clock(this_rq);

	/*
	 * 不要将任务拉向非活动 CPU...
	 */
	if (!cpu_active(this_cpu))
		return 0;

	/*
	 * 当前任务在 CPU 上运行，避免其被选中进行负载均衡。
	 * 同时中断/抢占仍然被禁用，避免进一步的调度器活动。
	 */
	rq_unpin_lock(this_rq, rf);

	rcu_read_lock();
	sd = rcu_dereference_check_sched_domain(this_rq->sd);

	/*
	 * 如果调度域未过载，或者当前 CPU 的平均空闲时间小于
	 * 调度域的最大负载均衡时间开销，则跳过负载均衡。
	 */
	if (!get_rd_overloaded(this_rq->rd) ||
		(sd && this_rq->avg_idle < sd->max_newidle_lb_cost)) {

		if (sd)
			update_next_balance(sd, &next_balance);
		rcu_read_unlock();

		goto out;
	}
	rcu_read_unlock();

	// 释放运行队列锁
	raw_spin_rq_unlock(this_rq);

	t0 = sched_clock_cpu(this_cpu); // 记录开始时间
	sched_balance_update_blocked_averages(this_cpu); // 更新阻塞任务的负载均衡统计

	rcu_read_lock();
	for_each_domain(this_cpu, sd) {
		u64 domain_cost;

		// 更新调度域的下一次负载均衡时间
		update_next_balance(sd, &next_balance);

		// 如果当前 CPU 的平均空闲时间不足以进行负载均衡，则停止
		if (this_rq->avg_idle < curr_cost + sd->max_newidle_lb_cost)
			break;

		// 如果调度域支持新空闲负载均衡，则尝试拉取任务
		if (sd->flags & SD_BALANCE_NEWIDLE) {

			pulled_task = sched_balance_rq(this_cpu, this_rq,
						   sd, CPU_NEWLY_IDLE,
						   &continue_balancing);

			t1 = sched_clock_cpu(this_cpu); // 记录结束时间
			domain_cost = t1 - t0; // 计算调度域的负载均衡时间开销
			update_newidle_cost(sd, domain_cost); // 更新调度域的负载均衡时间开销

			curr_cost += domain_cost; // 累加总开销
			t0 = t1; // 更新开始时间
		}

		/*
		 * 如果成功拉取任务，或者不需要继续负载均衡，则停止搜索任务。
		 */
		if (pulled_task || !continue_balancing)
			break;
	}
	rcu_read_unlock();

	// 重新获取运行队列锁
	raw_spin_rq_lock(this_rq);

	// 如果负载均衡时间开销超过当前运行队列的最大空闲负载均衡时间开销，则更新
	if (curr_cost > this_rq->max_idle_balance_cost)
		this_rq->max_idle_balance_cost = curr_cost;

	/*
	 * 在浏览调度域期间，我们释放了运行队列锁，可能有任务在此期间入队。
	 * 如果我们不再处于空闲状态，则假装我们拉取了一个任务。
	 */
	if (this_rq->cfs.h_nr_queued && !pulled_task)
		pulled_task = 1;

	/* 是否存在高优先级任务 */
	if (this_rq->nr_running != this_rq->cfs.h_nr_queued)
		pulled_task = -1;

out:
	/* 将下一次负载均衡时间向前移动 */
	if (time_after(this_rq->next_balance, next_balance))
		this_rq->next_balance = next_balance;

	// 如果成功拉取任务，则清除 idle_stamp，否则更新空闲负载均衡状态
	if (pulled_task)
		this_rq->idle_stamp = 0;
	else
		nohz_newidle_balance(this_rq);

	// 重新固定运行队列锁
	rq_repin_lock(this_rq, rf);

	return pulled_task;
}
```

### 负载均衡的核心函数 sched_balance_rq

负载均衡的核心函数为 sched_balance_rq。

在该函数中进行了如下工作：


![alt text](image-2.png)

#### 判断是否需要进行负载均衡 should_we_balance

检查是否需要进行负载均衡，这里需要检查以下几个场景

|场景 | 触发条件 | 典型触发路径 | 优先级 |
|----|----|----|----|
|新空闲CPU| CPU刚空闲且无待处理任务| 任务退出/定时器中断检测| 最高|
|完全空闲核心| 非SMT域中所有硬件线程空闲| MC/NUMA层均衡| 高|
|部分空闲SMT核心| SMT域中核心有忙碌线程，当前是第一个空闲SMT| SMT层均衡| 中|
|组首选CPU| 无其他匹配条件，当前CPU是组内首选| 热插拔/隔离后的回退机制| 低|

#### 寻找需要迁移任务的源调度组

使用 group_type 来表示调度组现在忙的状态

```c
enum group_type {
	group_has_spare = 0,
	group_fully_busy,
	group_misfit_task,
	group_smt_balance,
	group_asym_packing,
	group_imbalanced,
	group_overloaded
};
```

​|​枚举值​​	​|​优先级​​	​|​状态描述​​	​|​触发条件​​	|​​典型场景​​	​|​负载均衡动作​​|
|----|----|----|----|----|----|----|
|group_has_spare|	0（最低）	|组内有空闲算力可运行更多任务|	组内空闲CPU占比较高（如idle_cpus > group_weight/2）	|4核CPU组中有2个空闲核	|接收其他组迁移来的任务|
|group_fully_busy|1	|组内CPU全忙但无资源竞争|	所有CPU的nr_running > 0且无等待任务	|每个CPU运行1个非CPU密集型任务|	通常不触发迁移|
|group_misfit_task|	2	|存在任务超出当前CPU算力|	任务需求 > CPU最大能力（rq->misfit_task_load被设置）|	视频编码任务运行在手机小核上	|​​必须​​迁移至更强CPU（如小核→大核）
|group_smt_balance|	3	|需要优化SMT超线程组的负载|	物理核的所有超线程都忙，且系统有空闲物理核|	Intel CPU双超线程100%负载，另一物理核空闲|迁移任务至空闲物理核缓解争用|
|group_asym_packing|	4	|存在高性能闲置CPU（需SD_ASYM_PACKING标志）|	大核空闲时，小核仍有任务运行	|ARM big.LITTLE架构中大核空闲	|将任务"打包"到大核以提高性能|
|group_imbalanced|	5	|因任务亲和性限制导致负载不均|	任务被cpuset或sched_setaffinity限制到部分CPU	|数据库进程绑定到前4核导致后4核闲置|	尝试突破亲和性限制做强制均衡|
|group_overloaded|	6（最高）	|CPU过载无法满足任务需求|	运行队列中有多个任务等待（sum_nr_running > 1）	|单核上运行10个计算密集型线程|	分散任务到其他组|


#### 寻找需要迁移任务的源调度队列

#### 进行任务迁移

```c
more_balance:
		rq_lock_irqsave(busiest, &rf);
		update_rq_clock(busiest);

		/*
		 * cur_ld_moved - 当前迭代中迁移的负载
		 * ld_moved     - 跨迭代累计迁移的负载
		 */
		cur_ld_moved = detach_tasks(&env);

		/*
		 * 已从 busiest_rq 分离一些任务。每个任务都被标记为 "TASK_ON_RQ_MIGRATING"，
		 * 因此可以安全地解锁 busiest->lock，并确保没有人可以并行操作这些任务。
		 * 详情请参阅 task_rq_lock() 系列函数。
		 */
		rq_unlock(busiest, &rf);

		if (cur_ld_moved) {
			attach_tasks(&env);
			ld_moved += cur_ld_moved;
		}

		local_irq_restore(rf.flags);

		if (env.flags & LBF_NEED_BREAK) {
			env.flags &= ~LBF_NEED_BREAK;
			goto more_balance;
		}

		/*
		 * 重新访问无法迁移到 dst_cpu 的 src_cpu 上的任务，
		 * 并将它们迁移到调度组中的其他 dst_cpu 上。
		 * 我们对同一 src_cpu 的迭代次数上限取决于调度组中的 CPU 数量。
		 */
		if ((env.flags & LBF_DST_PINNED) && env.imbalance > 0) {

			/* 防止通过 env 的 CPUs 重新选择 dst_cpu */
			__cpumask_clear_cpu(env.dst_cpu, env.cpus);

			env.dst_rq	 = cpu_rq(env.new_dst_cpu);
			env.dst_cpu	 = env.new_dst_cpu;
			env.flags	&= ~LBF_DST_PINNED;
			env.loop	 = 0;
			env.loop_break	 = SCHED_NR_MIGRATE_BREAK;

			/*
			 * 返回 "more_balance" 而不是 "redo"，因为需要继续处理同一个 src_cpu。
			 */
			goto more_balance;
		}

		/*
		 * 由于任务的亲和性，我们未能达到平衡。
		 */
		if (sd_parent) {
			int *group_imbalance = &sd_parent->groups->sgc->imbalance;

			if ((env.flags & LBF_SOME_PINNED) && env.imbalance > 0)
				*group_imbalance = 1;
		}

		/* 此运行队列上的所有任务都因 CPU 亲和性而被固定 */
		if (unlikely(env.flags & LBF_ALL_PINNED)) {
			__cpumask_clear_cpu(cpu_of(busiest), cpus);
			/*
			 * 仅当当前调度域级别仍有活跃 CPU 可作为最繁忙的 CPU 来拉取负载，
			 * 且这些 CPU 不包含在接收迁移负载的目标组中时，
			 * 才继续负载均衡。
			 */
			if (!cpumask_subset(cpus, env.dst_grpmask)) {
				env.loop = 0;
				env.loop_break = SCHED_NR_MIGRATE_BREAK;
				goto redo;
			}
			goto out_all_pinned;
		}
	}

	if (!ld_moved) {
		schedstat_inc(sd->lb_failed[idle]);
		/*
		 * 仅在周期性负载均衡时增加失败计数器。
		 * 我们不希望频繁的新空闲负载均衡污染失败计数器，
		 * 导致过多的 cache_hot 迁移和主动负载均衡。
		 *
		 * 同样，migration_misfit 不涉及负载/利用率迁移，
		 * 不应污染 nr_balance_failed。
		 */
		if (idle != CPU_NEWLY_IDLE &&
			env.migration_type != migrate_misfit)
			sd->nr_balance_failed++;

		if (need_active_balance(&env)) {
			unsigned long flags;

			raw_spin_rq_lock_irqsave(busiest, flags);

			/*
			 * 如果 busiest CPU 上的当前任务无法迁移到 this_cpu，
			 * 则不要触发 active_load_balance_cpu_stop：
			 */
			if (!cpumask_test_cpu(this_cpu, busiest->curr->cpus_ptr)) {
				raw_spin_rq_unlock_irqrestore(busiest, flags);
				goto out_one_pinned;
			}

			/* 记录至少找到一个任务可以在 this_cpu 上运行 */
			env.flags &= ~LBF_ALL_PINNED;

			/*
			 * ->active_balance 同步访问 ->active_balance_work。
			 * 一旦设置，仅在主动负载均衡完成后清除。
			 */
			if (!busiest->active_balance) {
				busiest->active_balance = 1;
				busiest->push_cpu = this_cpu;
				active_balance = 1;
			}

			preempt_disable();
			raw_spin_rq_unlock_irqrestore(busiest, flags);
			if (active_balance) {
				stop_one_cpu_nowait(cpu_of(busiest),
					active_load_balance_cpu_stop, busiest,
					&busiest->active_balance_work);
			}
			preempt_enable();
		}
	} else {
		sd->nr_balance_failed = 0;
	}

	if (likely(!active_balance) || need_active_balance(&env)) {
		/* 我们处于不平衡状态，因此重置负载均衡间隔 */
		sd->balance_interval = sd->min_interval;
	}
```

这里在迁移时有四种状态：

* LBF_NEED_BREAK是用来做中场休息的，其实还没有迁移完，所以这时候只是释放rq锁，然后再回去重新来迁移进程。

* LBF_DST_PINNED是指要被迁移的task因为affinity的原有没法迁移，将env.dst_cpu换成env.new_dst_cpu再去试试。

* LBF_SOME_PINNED是指因为affinity的原有无法迁移进程，让父调度域去解决。

* LBF_ALL_PINNED说明所有task都pin住了，没法迁移，清除在排除掉busiest group的cpu尝试重新选择busiest group从头再来。

## 为task选择cpu

有三种情况需要为task选择cpu：刚创建的进程（fork），刚exec的进程（exec），刚被唤醒的进程（wakeup）他们都会调用select_task_rq，对于cfs，就是select_task_rq_fair。
