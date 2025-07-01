# schedutil

schedutil 是 Linux 内核中一种 ​​基于调度器实时利用率反馈的动态调频策略，直接利用调度器（CFS）计算的 ​​实时 CPU 利用率来决定目标频率，无需额外的采样或预测算法。

核心思想：
* ​“需要多少算力，就给多少频率”​​，避免静态频率策略的保守或激进问题。
* 通过减少调频延迟和消除中间层（如 ondemand 的采样周期）来提升能效和性能。

![alt text](../image/schedutil.png)

schedutil 在初始化时会注册回调函数到调度器的负载跟踪模块。当调度器的负载发生变化时，就会调用回调函数。在回调函数中会检查当前 CPU 频率与 CPU 负载是否匹配，如果不匹配则需要重新调整 CPU 的频率。

## 数据结构

在 cpu 调频子系统中会使用 cpu_dbs_info 来记录

```c
struct cpu_dbs_info {
	u64 prev_cpu_idle;
	u64 prev_update_time;
	u64 prev_cpu_nice;
	/*
	 * Used to keep track of load in the previous interval. However, when
	 * explicitly set to zero, it is used as a flag to ensure that we copy
	 * the previous load to the current interval only once, upon the first
	 * wake-up from idle.
	 */
	unsigned int prev_load;
	struct update_util_data update_util;
	struct policy_dbs_info *policy_dbs;
};
```

## 接口

在 schedutil_gov 中注册了如下函数来应对不同的事件

```c
// 定义 schedutil 调速器的 cpufreq_governor 结构体
struct cpufreq_governor schedutil_gov = {
    .name			= "schedutil", // 调速器名称
    .owner			= THIS_MODULE, // 所属模块
    .flags			= CPUFREQ_GOV_DYNAMIC_SWITCHING, // 支持动态切换的标志
    .init			= sugov_init, // 初始化函数
    .exit			= sugov_exit, // 退出函数
    .start			= sugov_start, // 启动函数
    .stop			= sugov_stop, // 停止函数
    .limits			= sugov_limits, // 频率限制更新函数
};
```
### 初始化 sugov

这里根据在初始化过程中的作用分为两个阶段：

```
sugov_init()
├─ 阶段一：基础初始化
│   ├─ 检查 policy->governor_data
│   ├─ 启用 fast_switch
│   ├─ 分配 sg_policy
│   └─ 创建线程（慢速路径）
│       ├─ 成功 → 进入阶段二
│       └─ 失败 → 回滚（free_sg_policy → disable_fast_switch）
│
└─ 阶段二：Tunables 初始化
    ├─ 加全局锁
    ├─ 复用或分配 tunables
    │   ├─ 成功 → 完成初始化
    │   └─ 失败 → 回滚（stop_kthread → 阶段一回滚）
    └─ 释放锁
```

**阶段一：基础资源分配与快速切换启用​**

完成​​策略（policy）的基础初始化​​，包括启用快速切换、分配内存、创建线程等。
若此阶段失败，需回滚已分配的资源（如内存、线程）。

```c
	/* 1.确保此策略尚未初始化调速器 */
	if (policy->governor_data)
		return -EBUSY;

	/* 2.启用策略的快速频率切换 */
	cpufreq_enable_fast_switch(policy);

	/* 3.为 schedutil 策略结构分配内存 */
	sg_policy = sugov_policy_alloc(policy);
	if (!sg_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

	/* 4.如果需要，为慢路径更新创建内核线程 */
	ret = sugov_kthread_create(sg_policy);
	if (ret)
		goto free_sg_policy;

```

**1.检查是否有旧的调频策略未卸载** 

struct cpufreq_policy 的 governor_data 会指向当前 governor policy 对象，要把 sugov设置为当前 governor，那么旧的 governor 应该完成 stop 和 exit 动作。如果此时旧的策略已经被卸载那么 governor_data 为空，可以对策略进行初始化。否则说明旧的策略还存在无法对当前策略进行初始化，返回 -EBUSY。

**2.使能 fast switch 功能（快路径更新）** 

调用 cpufreq_enable_fast_switch 来使能 fast switch 功能。​​Fast Switch（快速频率切换）​​ 是一种优化机制，允许 CPU 频率在​​调度器上下文（如任务切换时）直接调整​​，而无需经过传统的异步通知流程。它的核心目标是​​降低频率切换的延迟​​，提高系统的响应速度和能效。
这里的使能只是指 sugov policy 层 enable fast switch，具体是否支持还要看底层 cpufreq 驱动。这里为了标志底层是否支持，引入了 fast_switch_possible 和 fast_switch_enabled。两个参数。

```c
	/*
	 * 快速切换标志：
	 * - fast_switch_possible：如果驱动程序能够保证可以在共享该策略的任何CPU上更改频率，
	 *   并且更改将影响所有策略CPU，则应由驱动程序设置。
	 * - fast_switch_enabled：由支持快速频率切换的调节器设置，
	 *   通过调用 cpufreq_enable_fast_switch() 实现。
	 */
	bool			fast_switch_possible;
	bool			fast_switch_enabled;
```

如果底层驱动支持快速切频功能，那么cpufreq driver必须提供fast_switch的回调函数，这时候cpufreq policy的fast_switch_possible 等于true，表示驱动支持任何上下文（包括中断上下文）的频率切换。只有上下打通（上指governor，下指driver），CPU 频率切换才走 fast switch 路径。

且 fast switch 和 cpufreq transition notifier 之间具有互斥性

cpufreq transition notifier​​：

这是传统的频率切换通知机制。当 CPU 频率即将改变（PRECHANGE）或已完成改变（POSTCHANGE）时，内核会通过通知链（notifier chain）调用其他模块注册的回调函数（callback）。这些回调可能涉及复杂的操作（如调整时钟、电源管理等），且​​可能是阻塞的​​（例如等待硬件响应或持有锁）。

​​fast switch​​：

这是为了快速切换频率而设计的机制，要求切换过程必须是​​非阻塞且原子化​​的（不能睡眠、不能被打断）。因此，它​​无法兼容传统的 notifier 机制​​，因为 notifier 的回调可能阻塞，而 fast switch 不允许等待。

虽然 fast switch 移除了 notifier 机制，但​​频率切换的串行化​​（防止并发修改）仍需保证。因此：

调用 cpufreq_driver_fast_switch 的模块（如 governor）必须​​自行实现同步​​（例如通过锁或原子操作），确保频率切换的原子性和线程安全。

快速路径切换函数的实现：

```c
/**
 * cpufreq_driver_fast_switch - 执行快速 CPU 频率切换。
 * @policy: 要切换频率的 cpufreq 策略。
 * @target_freq: 要设置的新频率（可能是近似值）。
 *
 * 执行快速频率切换，无需进入睡眠状态。
 *
 * 此函数调用的驱动程序的 ->fast_switch() 回调必须适合在 RCU-sched
 * 读取侧临界区内调用，并且应选择大于或等于 @target_freq 的最小可用频率
 * （CPUFREQ_RELATION_L）。
 *
 * 如果 policy->fast_switch_enabled 未设置，则不得调用此函数。
 *
 * 调用此函数的调节器必须保证它不会对同一策略并行调用两次，
 * 并且不会与同一策略的 ->target() 或 ->target_index() 并行调用。
 *
 * 返回为 CPU 设置的实际频率。
 *
 * 如果驱动程序的 ->fast_switch() 回调返回 0 以指示错误条件，
 * 则必须保留硬件配置。
 */
unsigned int cpufreq_driver_fast_switch(struct cpufreq_policy *policy,
					unsigned int target_freq)
{
	unsigned int freq;
	int cpu;

	target_freq = clamp_val(target_freq, policy->min, policy->max);
	freq = cpufreq_driver->fast_switch(policy, target_freq);

	if (!freq)
		return 0;

	policy->cur = freq;
	arch_set_freq_scale(policy->related_cpus, freq,
				arch_scale_freq_ref(policy->cpu));
	cpufreq_stats_record_transition(policy, freq);

	if (trace_cpu_frequency_enabled()) {
		for_each_cpu(cpu, policy->cpus)
			trace_cpu_frequency(freq, cpu);
	}

	return freq;
}
EXPORT_SYMBOL_GPL(cpufreq_driver_fast_switch);
```

该函数会在更新cpu频率时被调用。sugov_update_single_freq/sugov_update_shared -> cpufreq_driver_fast_switch 。

**3.分配内存对象**

调用sugov_policy_alloc分配sugov policy对象，通过其policy成员建立和cpufreq framework的关联。

**4.慢路径更新**



​**​阶段二：Tunables 初始化与全局管理​​**

处理与 ​​tunables（可调参数）​​ 相关的逻辑，包括复用全局参数或创建新参数。此阶段失败需额外回滚线程和全局锁。

这里使用 sugov_tunables 结构体来记录频率调整的信息。

```c
// 定义 schedutil 调速器的 tunables 结构体
struct sugov_tunables {
	struct gov_attr_set	attr_set; // 属性集合，用于 sysfs 接口
	unsigned int		rate_limit_us; // 频率更新的速率限制（以微秒为单位）
};
```

全局 vs 每策略（per-cluster）的 tunables​​

​​(1) 全局 tunables（global_tunables）​​
* ​所有 policy 共享同一组参数​​：系统中所有 CPU 调频域（cluster）共用同一个 global_tunables，修改任一参数会影响所有 policy。
* ​适用场景​​：硬件平台所有 cluster 的调频行为需要完全一致（较少见）。

​​(2) 每策略 tunables（per-policy）​​
* ​每个 policy 有自己的独立参数​​：不同 cluster 可以配置不同的调频参数（例如大核和小核设置不同的 rate_limit_us）。
* ​手机典型场景​​：现代手机通常采用 ​​big.LITTLE 架构​​（如 1+3+4 三簇），不同 cluster 的负载特性和性能需求不同，因此需要独立的 tunables。

```c
	/* 检查全局 tunables 是否已存在 */
	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		/* 重用现有的全局 tunables */
		policy->governor_data = sg_policy;
		sg_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &sg_policy->tunables_hook);
		goto out;
	}

	/* 为调速器分配新的 tunables */
	tunables = sugov_tunables_alloc(sg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}

	/* 设置频率更新的默认速率限制 */
	tunables->rate_limit_us = cpufreq_policy_transition_delay_us(policy);

	/* 将策略与调速器数据关联 */
	policy->governor_data = sg_policy;
	sg_policy->tunables = tunables;

```

**1.分配tunables结构**

调用 sugov_tunables_alloc 函数分配 sugov_tunables 结构并将分配好的结构体注册到 policy 结构体中。该函数不仅分配内存还会建立 sugov_tunables 与 policy 之间的双向联系

**2.cpufreq_policy_transition_delay_us**

​​关于 CPU 调频间隔的三个控制参数​​
这段描述解释了 Linux 内核中 ​​CPU 频率调节（cpufreq）​​ 的三个关键时间参数，它们共同决定了调频的最小时间间隔，以避免频繁切换频率导致性能或稳定性问题。以下是详细解析：

​​(1) 硬件调频延迟（transition_latency）​​
* ​定义​​：硬件从当前频率（F1）切换到目标频率（F2）并稳定下来所需的时间。
* ​存储位置​​：struct cpufreq_policy → cpuinfo.transition_latency（单位：纳秒 ns）。
* ​作用​​：反映 ​​硬件的物理限制​​。

​​(2) Governor 调频间隔（rate_limit_us / freq_update_delay_ns）​​
* ​定义​​：​sugov（或其他 governor）​​ 设定的最小调频间隔，避免软件层过于频繁地触发调频。
* ​存储位置​​：struct sugov_tunables → rate_limit_us（单位：微秒 µs）。最终会转换freq_update_delay_ns（纳秒 ns）供内核使用。
* ​作用​​：控制 ​​governor 调频的节奏​。
  
​​(3) 驱动调频间隔（transition_delay_us）​​
* ​定义​​：​底层 cpufreq 驱动​​ 建议的最小调频间隔，可能基于硬件特性或经验值。
* ​存储位置​​：struct cpufreq_policy → transition_delay_us（单位：微秒 µs）。
* ​作用​​：驱动可以 ​​覆盖硬件默认值​​，提供更合理的调频间隔。

**3.建立cpufreq framework和sugov的关联（初始化governor_data）**

**4.初始化可调参数的sysfs接口**

### 启动 sugov

在初始化过程中会对回调函数进行注册。在 sugov_start 中会调用 cpufreq_add_update_util_hook 将回调函数注册到percpu的结构体当中。

```c
static int sugov_start(struct cpufreq_policy *policy)
{
    struct sugov_policy *sg_policy = policy->governor_data; // 获取当前CPU策略的schedutil私有数据（sg_policy）
    void (*uu)(struct update_util_data *data, u64 time, unsigned int flags); // 声明回调函数指针，用于后续选择具体的调频更新函数
    unsigned int cpu; // CPU核心编号变量

    /* 初始化策略参数 */
    // 将用户配置的rate_limit_us（微秒）转换为纳秒级延迟
    sg_policy->freq_update_delay_ns = sg_policy->tunables->rate_limit_us * NSEC_PER_USEC;
    sg_policy->last_freq_update_time = 0; // 清零上次频率更新时间戳
    sg_policy->next_freq = 0; // 初始化下一个目标频率为0（表示未设置）
    sg_policy->work_in_progress = false; // 标记当前没有正在进行的频率更新工作
    sg_policy->limits_changed = false; // 标记频率限制未发生变化
    sg_policy->cached_raw_freq = 0; // 缓存原始频率值为0

    /* 检查是否需要更新频率限制 */
    // 测试CPU驱动是否需要动态更新频率限制
    sg_policy->need_freq_update = cpufreq_driver_test_flags(CPUFREQ_NEED_UPDATE_LIMITS);

    /* 选择调频更新函数 */
    if (policy_is_shared(policy)) {
        // 如果是多核共享调频策略（如Intel Turbo Boost），使用共享更新函数
        uu = sugov_update_shared;
    } else if (policy->fast_switch_enabled && cpufreq_driver_has_adjust_perf()) {
        // 如果支持快速频率切换和性能调整接口，使用单核性能模式更新
        uu = sugov_update_single_perf;
    } else {
        // 默认使用标准单核频率更新函数
        uu = sugov_update_single_freq;
    }

    /* 为每个CPU核心注册调频回调 */
    for_each_cpu(cpu, policy->cpus) {
        // 获取当前CPU的schedutil私有数据
        struct sugov_cpu *sg_cpu = &per_cpu(sugov_cpu, cpu);

        // 清零初始化CPU数据
        memset(sg_cpu, 0, sizeof(*sg_cpu));
        
        // 记录CPU编号
        sg_cpu->cpu = cpu;
        
        // 关联到策略数据
        sg_cpu->sg_policy = sg_policy;
        
        // 关键步骤：将回调函数注册到调度器
        // 当调度器需要更新CPU利用率时，会调用uu指向的函数
        cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util, uu);
    }
    return 0;
}
void cpufreq_add_update_util_hook(int cpu, struct update_util_data *data,
			void (*func)(struct update_util_data *data, u64 time,
				     unsigned int flags))
{
	if (WARN_ON(!data || !func))
		return;

	if (WARN_ON(per_cpu(cpufreq_update_util_data, cpu)))
		return;

	data->func = func;
	rcu_assign_pointer(per_cpu(cpufreq_update_util_data, cpu), data);
}
```

### Sugov的停止

sugov_stop执行逻辑大致如下：

A、遍历该sugov policy（cluster）中的所有cpu，调用cpufreq_remove_update_util_hook注销sugov cpu的调频回调函数

B、sugov_stop之后可能会调用sugov_exit来释放该governor所持有的资源，包括update_util_data对象。通过synchronize_rcu函数可以确保之前对update_util_data对象的并发访问都已经离开了临界区，从而后续可以安全释放。

C、在不支持fast switch模式的时候，我们需要把pending状态状态的irq work和kthread work处理完毕，为后续销毁线程做准备

### Sugov的退出

sugov_exit主要功能是释放申请的资源，具体执行逻辑大致如下：

A、断开cpufreq framework中的cpufreq policy和sugover的关联（即将其governor_data设置为NULL）

B、调用sugov_tunables_free释放可调参数的内存（如果是多个policy共用一个可调参数对象，那么需要通过引用计数来判断是否还有sugov policy引用该对象）

C、调用sugov_kthread_stop来消耗用于sugov调频的内核线程（仅用在不支持fast switch场景）

D、调用sugov_policy_free释放sugov policy的内存

E、调用cpufreq_disable_fast_switch来禁止本policy上的fast switch。

### Sugov的限频

Sugov 的限频会在限频参数有修改的时候被触发。这里通过 cpufreq_set_policy -> cpufreq_governor_limits -> governor->limits 最终调到注册的 sugov_limits。

```c
static void sugov_limits(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;

	// 如果未启用快速切换，则加锁更新策略限制
	if (!policy->fast_switch_enabled) {
		mutex_lock(&sg_policy->work_lock);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&sg_policy->work_lock);
	}

	/*
	 * 下面的 limits_changed 更新必须在 cpufreq_set_policy() 中
	 * 更新策略限制之前完成，否则可能会错过策略限制的更新。
	 * 使用写内存屏障确保这一点。
	 *
	 * 这与 sugov_should_update_freq() 中的内存屏障配对。
	 */
	smp_wmb();

	// 设置 limits_changed 标志为 true
	WRITE_ONCE(sg_policy->limits_changed, true);
}
```
A、对于不支持fast switch的情况下，立刻调用cpufreq_policy_apply_limits函数使用最新的max和min来修正当前cpu频率，同时标记sugov policy中的limits_changed成员。

B、对于支持fast switch的情况下，仅仅标记sugov policy中的limits_changed成员即可，并不立刻进行频率修正。后续在调用cpufreq_update_util函数进行调频的时候会强制进行一次频率调整。

## 更新负载

在不同的调度器中更新负载时都会调用 cpufreq_update_util 函数来更新负载。

以 cfs 中的调用为例：

```c
static inline void cfs_rq_util_change(struct cfs_rq *cfs_rq, int flags)
{
	struct rq *rq = rq_of(cfs_rq);

	if (&rq->cfs == cfs_rq) {
		/*
		 * There are a few boundary cases this might miss but it should
		 * get called often enough that that should (hopefully) not be
		 * a real problem.
		 *
		 * It will not get called when we go idle, because the idle
		 * thread is a different class (!fair), nor will the utilization
		 * number include things like RT tasks.
		 *
		 * As is, the util number is not freq-invariant (we'd have to
		 * implement arch_scale_freq_capacity() for that).
		 *
		 * See cpu_util_cfs().
		 */
		cpufreq_update_util(rq, flags);
	}
}
```

再该函数中 cpufreq_update_util 会调用提前注册好的函数。

```c
static inline void cpufreq_update_util(struct rq *rq, unsigned int flags)
{
	struct update_util_data *data;

	data = rcu_dereference_sched(*per_cpu_ptr(&cpufreq_update_util_data,
						  cpu_of(rq)));
	if (data)
		data->func(data, rq_clock(rq), flags);
}
```

在这里注册的函数有 sugov_update_shared sugov_update_single_perf sugov_update_single_freq。

```c
// 更新单个 CPU 的频率
static void sugov_update_single_freq(struct update_util_data *hook, u64 time,
					 unsigned int flags)
{
	// 获取 sugov_cpu 结构体
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	// 获取 sugov_policy 结构体
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	// 缓存当前的原始频率
	unsigned int cached_freq = sg_policy->cached_raw_freq;
	// 最大 CPU 容量
	unsigned long max_cap;
	// 下一个频率
	unsigned int next_f;

	// 获取 CPU 的最大容量
	max_cap = arch_scale_cpu_capacity(sg_cpu->cpu);

	// 更新通用逻辑，如果返回 false 则无需更新频率
	if (!sugov_update_single_common(sg_cpu, time, max_cap, flags))
		return;

	// 根据当前利用率和最大容量计算下一个频率
	next_f = get_next_freq(sg_policy, sg_cpu->util, max_cap);

	// 如果需要保持频率，并且下一个频率小于当前频率，且不需要更新频率
	if (sugov_hold_freq(sg_cpu) && next_f < sg_policy->next_freq &&
		!sg_policy->need_freq_update) {
		// 使用当前频率作为下一个频率
		next_f = sg_policy->next_freq;

		// 恢复缓存的原始频率，因为 next_freq 已更改
		sg_policy->cached_raw_freq = cached_freq;
	}

	// 如果频率未改变，则无需更新
	if (!sugov_update_next_freq(sg_policy, time, next_f))
		return;

	/*
	 * 此代码在目标 CPU 的 rq->lock 下运行，因此不会在两个不同的 CPU 上
	 * 并发运行针对同一目标的代码，因此在快速切换情况下不需要获取锁。
	 */
	if (sg_policy->policy->fast_switch_enabled) {
		// 快速切换到下一个频率
		cpufreq_driver_fast_switch(sg_policy->policy, next_f);
	} else {
		// 使用锁保护延迟更新逻辑
		raw_spin_lock(&sg_policy->update_lock);
		sugov_deferred_update(sg_policy);
		raw_spin_unlock(&sg_policy->update_lock);
	}
}

// 更新单个 CPU 的性能
static void sugov_update_single_perf(struct update_util_data *hook, u64 time,
					 unsigned int flags)
{
	// 获取 sugov_cpu 结构体
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	// 保存之前的利用率
	unsigned long prev_util = sg_cpu->util;
	// 最大 CPU 容量
	unsigned long max_cap;

	/*
	 * 如果不支持频率不变性，则回退到“频率”路径，
	 * 因为利用率与性能级别之间的直接映射依赖于频率不变性。
	 */
	if (!arch_scale_freq_invariant()) {
		sugov_update_single_freq(hook, time, flags);
		return;
	}

	// 获取 CPU 的最大容量
	max_cap = arch_scale_cpu_capacity(sg_cpu->cpu);

	// 更新通用逻辑，如果返回 false 则无需更新性能
	if (!sugov_update_single_common(sg_cpu, time, max_cap, flags))
		return;

	// 如果需要保持频率，并且当前利用率小于之前的利用率，则使用之前的利用率
	if (sugov_hold_freq(sg_cpu) && sg_cpu->util < prev_util)
		sg_cpu->util = prev_util;

	// 调整 CPU 的性能
	cpufreq_driver_adjust_perf(sg_cpu->cpu, sg_cpu->bw_min,
				   sg_cpu->util, max_cap);

	// 更新策略的最后一次频率更新时间
	sg_cpu->sg_policy->last_freq_update_time = time;
}

// 计算共享 CPU 的下一个频率
static unsigned int sugov_next_freq_shared(struct sugov_cpu *sg_cpu, u64 time)
{
	// 获取 sugov_policy 和 cpufreq_policy 结构体
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	// 初始化利用率和最大容量
	unsigned long util = 0, max_cap;
	unsigned int j;

	// 获取当前 CPU 的最大容量
	max_cap = arch_scale_cpu_capacity(sg_cpu->cpu);

	// 遍历策略中的所有 CPU
	for_each_cpu(j, policy->cpus) {
		// 获取每个 CPU 的 sugov_cpu 结构体
		struct sugov_cpu *j_sg_cpu = &per_cpu(sugov_cpu, j);
		unsigned long boost;

		// 应用 IO 等待提升
		boost = sugov_iowait_apply(j_sg_cpu, time, max_cap);
		// 获取 CPU 的利用率
		sugov_get_util(j_sg_cpu, boost);

		// 更新最大利用率
		util = max(j_sg_cpu->util, util);
	}

	// 根据最大利用率和最大容量计算下一个频率
	return get_next_freq(sg_policy, util, max_cap);
}

// 更新共享 CPU 的频率
static void
sugov_update_shared(struct update_util_data *hook, u64 time, unsigned int flags)
{
	// 获取 sugov_cpu 结构体
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	// 获取 sugov_policy 结构体
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	// 下一个频率
	unsigned int next_f;

	// 加锁保护共享策略的更新
	raw_spin_lock(&sg_policy->update_lock);

	// 更新 IO 等待提升状态
	sugov_iowait_boost(sg_cpu, time, flags);
	// 更新最后一次更新时间
	sg_cpu->last_update = time;

	// 忽略 DL 速率限制
	ignore_dl_rate_limit(sg_cpu);

	// 如果需要更新频率
	if (sugov_should_update_freq(sg_policy, time)) {
		// 计算共享 CPU 的下一个频率
		next_f = sugov_next_freq_shared(sg_cpu, time);

		// 如果频率未改变，则无需更新
		if (!sugov_update_next_freq(sg_policy, time, next_f))
			goto unlock;

		// 如果启用了快速切换，则直接切换到下一个频率
		if (sg_policy->policy->fast_switch_enabled)
			cpufreq_driver_fast_switch(sg_policy->policy, next_f);
		else
			// 否则延迟更新频率
			sugov_deferred_update(sg_policy);
	}

unlock:
	// 解锁
	raw_spin_unlock(&sg_policy->update_lock);
}
```

**三个函数的区别与联系**


|函数名|作用对象| 主要功能| 关键区别点|
|----|----|----|----|
|sugov_update_single_freq| 单个CPU| 更新单个CPU的频率（基于利用率计算）| 仅处理频率，不涉及性能调整|
|sugov_update_single_perf| 单个CPU| 更新单个CPU的性能（若支持频率不变性），否则回退到 _freq 逻辑| 支持性能调优，依赖硬件特性|
|sugov_update_shared| 共享CPU簇| 更新整个CPU簇（多核）的频率，基于所有CPU的最大利用率计算频率| 需加锁保护共享策略，避免竞争|

_perf 函数通过 cpufreq_driver_adjust_perf() 直接调整性能级别（如Intel HWP），而 _freq 仅修改频率。

共同目标​​：

* 均用于动态调整CPU频率/性能，响应负载变化（通过 update_util_data 钩子触发）。
* 最终可能调用 cpufreq_driver_fast_switch() 或延迟更新逻辑。
  
​​逻辑复用​​：
* sugov_update_single_perf 在​​不支持频率不变性​​时，直接调用 sugov_update_single_freq。
* 两者共享 sugov_update_single_common() 通用逻辑（检查是否需要更新）。

​​性能与频率的关联​​：
* _perf 函数通过 cpufreq_driver_adjust_perf() 直接调整性能级别（如Intel HWP），而 _freq 仅修改频率。

**频率不变性​​**

频率不变性​​是指 ​​CPU利用率（utilization）的计算与当前CPU运行频率无关​​。换句话说，无论CPU运行在1GHz还是3GHz，相同的实际工作量（如执行固定数量的指令）所计算出的利用率值应保持一致。

在动态调频（DVFS）场景中，CPU频率会随负载变化而升降。若缺乏频率不变性，利用率计算会失真：

1. ​高频时​​：CPU以3GHz运行，完成某任务仅需10ms，利用率计算为30%。
2. ​低频时​​：同一任务在1GHz下需30ms，利用率可能错误计算为90%。

​​结果​​：调度器和调频策略（如sugov）误判负载，导致次优决策（如不必要地升频）。频率不变性通过​​归一化​​处理，确保利用率反映真实负载，而非频率影响。


### Sugov的频率调整间隔

在调整频率前首先会 sugov_update_single_common sugov_should_update_freq 

```c
/**
 * sugov_should_update_freq - 判断是否需要更新 CPU 频率
 * @sg_policy: 指向 sugov_policy 结构体的指针
 * @time: 当前时间戳（以纳秒为单位）
 *
 * 返回值:
 * true  - 需要更新频率。
 * false - 不需要更新频率。
 */
static bool sugov_should_update_freq(struct sugov_policy *sg_policy, u64 time)
{
    s64 delta_ns;

    /* 检查当前 CPU 是否可以更新频率 */
    if (!cpufreq_this_cpu_can_update(sg_policy->policy))
        return false;

    /* 如果频率限制发生变化，强制更新频率 */
    if (unlikely(READ_ONCE(sg_policy->limits_changed))) {
        WRITE_ONCE(sg_policy->limits_changed, false);
        sg_policy->need_freq_update = true;
        smp_mb(); // 写内存屏障，确保标志位更新的可见性

        return true;
    }

    /* 计算当前时间与上次频率更新时间的间隔 */
    delta_ns = time - sg_policy->last_freq_update_time;

    /* 如果时间间隔超过最小更新间隔，则需要更新频率 */
    return delta_ns >= sg_policy->freq_update_delay_ns;
}
```

此函数用于确定是否需要更新 CPU 的频率。它会检查以下条件：
1. 当前 CPU 是否可以更新频率。
2. 是否有频率限制的变化。
3. 距离上次频率更新的时间间隔是否超过了设定的最小更新间隔。
 