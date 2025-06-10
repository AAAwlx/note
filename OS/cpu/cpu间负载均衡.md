# CPU 负载均衡

对于 CPU 的负载均衡大致分为两种

1. 为空闲的 CPU 核从繁忙的 CPU 核中拉取任务执行。
2. 为刚被创建/唤醒的任务选取 CPU 核。

## 触发CPU主动负载均衡的时机

触发负载均衡的路径有三种，分别为周期性负载均衡、NOHZ负载均衡、cpu在选择任务时自行负载均衡。 这里负载均衡采取的策略都是空闲的 CPU 在各个层级的调度域内从繁忙的 CPU 上主动拉取任务来达到调度域内的平衡。

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



### cpu在选择任务时自行负载均衡

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

## 为task选择cpu

有三种情况需要为task选择cpu：刚创建的进程（fork），刚exec的进程（exec），刚被唤醒的进程（wakeup）他们都会调用select_task_rq，对于cfs，就是select_task_rq_fair。
