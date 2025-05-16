# amd——pstate

amd-pstate 是 AMD CPU 性能扩展驱动程序，它在现代 AMD APU 和 Linux 内核中的 CPU 系列上引入了新的 CPU 频率控制机制。新机制基于协作处理器性能控制 （CPPC），它提供比传统 ACPI 硬件 P 状态更精细的频率管理。当前的 AMD CPU/APU 平台正在使用 ACPI P-states 驱动程序来管理 CPU 频率和时钟，仅在 3 个 P-state 中切换。CPPC 取代了 ACPI P 状态控制，并允许 Linux 内核使用灵活、低延迟的接口将性能提示直接传达给硬件。在 cpu 调频子系统中他作为一种硬件驱动来具体的设置 cpu 频率

## 性能控制

* auto_sel_en：是否允许硬件自主决定epp值（1允许，0禁止）
* max/min_perf： CPU 允许的最低性能级别
* des_perf：目标性能级别，设置后 cpu 会尽力将频率设置到接近目标频率
* epp：能耗性能偏好设置。调节 CPU 响应 DES_PERF 的激进程度，当偏向性能时，会保证不低于 des_perf 指定的值，如果是偏向能效时则会优先满足 epp 的设置，cpu 频率可能低于 des_perf 指定的值。

des_perf 与 epp 的优先级：

| ​​场景​​	 | 优先级 | 行为 |
| ------ | ------ | ------ |
| ​​des_perf > 标称频率​​（超频）	 | ​​des_perf 绝对优先​​	 | 强制尝试达到 des_perf 的目标频率，EPP 仅影响电压（可能因电压不足导致超频失败）。|
| ​​des_perf < 标称频率​​（超频）	 | ​​EPP 主导执行​​	 | 在满足 des_perf 的​​性能下限​​前提下，根据 EPP 调整实际频率（可能略低于 des_perf）。|
| ​​硬件限制（温度/TDP）​​	 | ​​硬件保护优先​​	 | 无视 des_perf 和 EPP，降频至安全范围。
 |

## 三种模式

1. Active 模式： 由用户态使用 energy_performance_preference 接口进行epp值的设置
   
   这是由 amd_pstate_epp 实现的低级固件控制模式 将 amd_pstate=active 的驱动程序传递给命令行中的内核。在此模式下，如果软件想要偏向 CPPC 固件的性能 （0x0） 或能效 （0xff），amd_pstate_epp 驱动程序会向硬件提供提示。然后 CPPC power 算法会计算运行时的工作负载，并根据电源和散热、Core voltage 和其他一些硬件条件调整实时 cores 频率。

2. Passive 模式： auto_sel_en = 0，epp = 0，des_perf由gov设置
   
   如果在命令行中将 amd_pstate=passive 传递给内核，则它将启用它。在此模式下，amd_pstate 驱动程序软件在 CPPC 性能量表中将所需的 QoS 目标指定为相对数字。这可以表示为名义性能 （infrastructure max） 的百分比。低于标称持续性能级别，所需性能表示受 Performance Reduction Tolerance 寄存器约束的处理器的平均性能级别。在标称性能水平之上，处理器必须至少提供所要求的标称性能，并在当前作条件允许的情况下提供更高的性能。

3. Guided 模式： auto_sel_en = 1，epp = 0，des_perf = 0
   如果 amd_pstate=guided 被传递给内核命令行选项，则此模式被激活。在此模式下，驱动程序请求最低和最高性能级别，平台自主选择此范围内并适合当前工作负载的性能级别。


### 三种模式间的切换 

1. ​​从 DISABLE 状态出发​​：
所有启用操作（→PASSIVE/ACTIVE/GUIDED）均调用 amd_pstate_register_driver
示例：echo passive > /sys/devices/system/cpu/amd_pstate/status

2. ​PASSIVE ↔ GUIDED 特殊路径​​：
双向转换使用 _without_dvr_change，避免驱动重复注册
仅更新内部控制逻辑（如 EPP 策略）

3. ​涉及 ACTIVE 的转换​​：
进出 ACTIVE 模式必须调用 change_driver_mode
因硬件需切换至 ACPI CPPC 主动控制模式

4. ​​禁用驱动​​：
所有状态 → DISABLE 均调用 unregister_driver
示例：echo disable > /sys/devices/system/cpu/amd_pstate/status

#### ​PASSIVE ↔ GUIDED

​PASSIVE ↔ GUIDED 之间使用 amd_pstate_change_mode_without_dvr_change 函数进行切换，这里主要的是为 cpu 设置 auto_sel_en 位

```c
static int amd_pstate_change_mode_without_dvr_change(int mode)
{
	int cpu = 0;

	cppc_state = mode;

	if (cpu_feature_enabled(X86_FEATURE_CPPC) || cppc_state == AMD_PSTATE_ACTIVE)
		return 0;

	for_each_present_cpu(cpu) {
		cppc_set_auto_sel(cpu, (cppc_state == AMD_PSTATE_PASSIVE) ? 0 : 1);
	}

	return 0;
}
```

#### 涉及 ACTIVE 的转换

在涉及到往 ACTIVE 的状态切换时，需要对底层的驱动进行切换。

```c
static int amd_pstate_change_driver_mode(int mode)
{
	int ret;

	ret = amd_pstate_unregister_driver(0);
	if (ret)
		return ret;

	ret = amd_pstate_register_driver(mode);
	if (ret)
		return ret;

	return 0;
}
```


## 两种 dirver 的实现

### amd_pstate_driver

针对 Passive 与 Guided 模式。在该模式下，由 gov 层中的 __cpufreq_driver_target 调用注册的回调函数 amd_pstate_target。在 gov 中会计算出 des_perf 并传递到 driver 层由 driver 进行设置。

```c
// 定义 AMD P-State 驱动程序结构
static struct cpufreq_driver amd_pstate_driver = {
	.flags		= CPUFREQ_CONST_LOOPS | CPUFREQ_NEED_UPDATE_LIMITS, // 驱动程序标志
	.verify		= amd_pstate_verify, // 用于验证策略限制的函数
	.target		= amd_pstate_target, // 用于设置目标频率的函数
	.fast_switch    = amd_pstate_fast_switch, // 用于快速频率切换的函数
	.init		= amd_pstate_cpu_init, // 用于初始化 CPU 驱动程序的函数
	.exit		= amd_pstate_cpu_exit, // 用于清理 CPU 驱动程序的函数
	.suspend	= amd_pstate_cpu_suspend, // 用于处理 CPU 挂起的函数
	.resume		= amd_pstate_cpu_resume, // 用于处理 CPU 恢复的函数
	.set_boost	= amd_pstate_set_boost, // 用于启用/禁用加速模式的函数
	.update_limits	= amd_pstate_update_limits, // 用于更新频率限制的函数
	.name		= "amd-pstate", // 驱动程序名称
	.attr		= amd_pstate_attr, // 驱动程序特定的属性
};

```

#### amd_pstate_target

amd_pstate_target -> amd_pstate_update_freq -> amd_pstate_update -> amd_pstate_update_perf 

在 amd_pstate_update_freq 中会根据 gov 层给出的 target_freq 来计算 des_perf

```c
des_perf = DIV_ROUND_CLOSEST(target_freq * cap_perf, cpudata->max_freq);
```

最后在 amd_pstate_update_perf 会真正设置 des_perf 的值。

```c
static inline int amd_pstate_update_perf(struct amd_cpudata *cpudata,
					  u8 min_perf, u8 des_perf,
					  u8 max_perf, u8 epp,
					  bool fast_switch)
{
	return static_call(amd_pstate_update_perf)(cpudata, min_perf, des_perf,
						   max_perf, epp, fast_switch);
}
```

### amd_pstate_epp_driver

针对 Active 模式，在 Active 模式下，driver 并不受 gov 层的控制，而是依据用户对于硬件值的设置作为参考，由 cpu 硬件自行调控。

```c
// 定义 AMD P-State EPP 驱动程序结构
static struct cpufreq_driver amd_pstate_epp_driver = {
	.flags		= CPUFREQ_CONST_LOOPS, // 驱动程序标志
	.verify		= amd_pstate_verify, // 用于验证策略限制的函数
	.setpolicy	= amd_pstate_epp_set_policy, // 用于设置策略的函数
	.init		= amd_pstate_epp_cpu_init, // 用于初始化 CPU 驱动程序的函数
	.exit		= amd_pstate_epp_cpu_exit, // 用于清理 CPU 驱动程序的函数
	.offline	= amd_pstate_epp_cpu_offline, // 用于处理 CPU 下线的函数
	.online		= amd_pstate_epp_cpu_online, // 用于处理 CPU 上线的函数
	.suspend	= amd_pstate_epp_suspend, // 用于处理 CPU 挂起的函数
	.resume		= amd_pstate_epp_resume, // 用于处理 CPU 恢复的函数
	.update_limits	= amd_pstate_update_limits, // 用于更新频率限制的函数
	.set_boost	= amd_pstate_set_boost, // 用于启用/禁用加速模式的函数
	.name		= "amd-pstate-epp", // 驱动程序名称
	.attr		= amd_pstate_epp_attr, // 驱动程序特定的属性
};
```

#### 暴露给用户态的接口 energy_performance_available_preferences 

energy_performance_preference 通过sys文件系统到这里后，最终会调用 amd_pstate_set_energy_pref_index 函数，在该函数中会通过用户输入的来模式选择epp的值。

```c
static int amd_pstate_set_energy_pref_index(struct cpufreq_policy *policy,
						int pref_index)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	u8 epp;

	/* 如果索引为 0，则使用默认的 EPP 值 */
	if (!pref_index)
		epp = cpudata->epp_default;
	else
		epp = epp_values[pref_index]; // 选择epp值

	/* 如果当前策略为性能策略，则不能设置非零的 EPP 值 */
	if (epp > 0 && cpudata->policy == CPUFREQ_POLICY_PERFORMANCE) {
		pr_debug("EPP 不能在性能策略下设置\n");
		return -EBUSY;
	}

	/* 如果启用了跟踪，则记录当前的 EPP 性能信息 */
	if (trace_amd_pstate_epp_perf_enabled()) {
		trace_amd_pstate_epp_perf(cpudata->cpu, cpudata->highest_perf,
					  epp,
					  FIELD_GET(AMD_CPPC_MIN_PERF_MASK, cpudata->cppc_req_cached),
					  FIELD_GET(AMD_CPPC_MAX_PERF_MASK, cpudata->cppc_req_cached),
					  policy->boost_enabled);
	}

	/* 设置 EPP 值 */
	return amd_pstate_set_epp(cpudata, epp);
}
```

值与其映射：

```c

#define AMD_CPPC_EPP_PERFORMANCE		0x00
#define AMD_CPPC_EPP_BALANCE_PERFORMANCE	0x80
#define AMD_CPPC_EPP_BALANCE_POWERSAVE		0xBF
#define AMD_CPPC_EPP_POWERSAVE			0xFF

static unsigned int epp_values[] = {
	[EPP_INDEX_DEFAULT] = 0,
	[EPP_INDEX_PERFORMANCE] = AMD_CPPC_EPP_PERFORMANCE,
	[EPP_INDEX_BALANCE_PERFORMANCE] = AMD_CPPC_EPP_BALANCE_PERFORMANCE,
	[EPP_INDEX_BALANCE_POWERSAVE] = AMD_CPPC_EPP_BALANCE_POWERSAVE,
	[EPP_INDEX_POWERSAVE] = AMD_CPPC_EPP_POWERSAVE,
 };

```

在 amd_pstate_set_epp 中调用底层的接口。

```c
static inline int amd_pstate_set_epp(struct amd_cpudata *cpudata, u8 epp)
{
	return static_call(amd_pstate_set_epp)(cpudata, epp);
}
```

## 硬件实现

amd-pstate 有两种类型的硬件实现：一种是 完全 MSR 支持 ，另一个是共享内存支持 。它可以使用 X86_FEATURE_CPPC 功能标志来指示不同的类型。

### 硬件特性
一些新的 Zen3 处理器（如 Cezanne）在设置 X86_FEATURE_CPPC CPU 功能标志时直接提供 MSR 寄存器。 amd-pstate 可以处理 MSR 寄存器，实现 CPUFreq 中的快速切换功能，可以减少中断上下文中频率控制的延迟。带有 pstate_xxx 前缀的函数表示对 MSR 寄存器的作。

1. MSR 寄存器的访问方式​​：MSR 是核私有寄存器​​
   
   * x86 架构规范​​：MSR 是 ​​每个 CPU 核心独立拥有​​ 的寄存器，不同核心的 MSR 值可以不同。例如：
      * IA32_PERF_CTL（性能控制）
      * IA32_HWP_REQUEST（硬件调频请求）
   * AMD 的 CPPC 相关 MSR（如 MSR_AMD_CPPC_CAP1）
     * ​访问方法​​：通过 rdmsr/wrmsr 指令操作，需指定目标 CPU 核心的 APIC ID。

如果未设置 X86_FEATURE_CPPC CPU 功能标志，则处理器支持共享内存解决方案。在这种情况下，amd-pstate 使用 cppc_acpi 帮助程序方法实现在 static_call 上定义的回调函数。带有 cppc_xxx 前缀的函数表示共享内存解决方案的 ACPI CPPC 帮助程序的作。

2. ​​PCC
   
   * 共享硬件资源​​
​      ​PCC 寄存器是平台级共享资源​​：
      当多个 CPU 核心同时通过 PCC 总线写入 DESIRED_PERF/MIN_PERF/MAX_PERF 寄存器时，若无同步机制，会导致：

      ​写覆盖​​：后写入的值覆盖前一次写入，导致策略失效。

      ​​时序错乱​​：硬件可能收到乱序的配置组合（如先收到 MAX_PERF=100 再收到 MIN_PERF=30，但实际期望相反顺序）。

   * 总线协议限制​​
      
      ​PCC 是单通道串行总线​​：类似 I2C 总线，同一时间只能处理一个命令（读或写）。若多核同时发起请求，会导致总线冲突。


### 代码实现

在函数 amd_pstate_init 中计会判断 X86_FEATURE_CPPC 是否被支持。

```c
if (cpu_feature_enabled(X86_FEATURE_CPPC)) {
	pr_debug("AMD CPPC MSR based functionality is supported\n");
} else {
	pr_debug("AMD CPPC shared memory based functionality is supported\n");
	static_call_update(amd_pstate_cppc_enable, shmem_cppc_enable);
	static_call_update(amd_pstate_init_perf, shmem_init_perf);
	static_call_update(amd_pstate_update_perf, shmem_update_perf);
	static_call_update(amd_pstate_get_epp, shmem_get_epp);
	static_call_update(amd_pstate_set_epp, shmem_set_epp);
}
```

如果 X86_FEATURE_CPPC 被支持：

```c
DEFINE_STATIC_CALL(amd_pstate_get_epp, msr_get_epp);
DEFINE_STATIC_CALL(amd_pstate_update_perf, msr_update_perf);
DEFINE_STATIC_CALL(amd_pstate_set_epp, msr_set_epp);
DEFINE_STATIC_CALL(amd_pstate_cppc_enable, msr_cppc_enable);
DEFINE_STATIC_CALL(amd_pstate_init_perf, msr_init_perf);
```

如果不被支持则会在初始化时注册。

以 amd_pstate_update_perf 为例

```c
// msr的实现
static int msr_update_perf(struct amd_cpudata *cpudata, u8 min_perf,
			   u8 des_perf, u8 max_perf, u8 epp, bool fast_switch)
{
	u64 value, prev;

	value = prev = READ_ONCE(cpudata->cppc_req_cached);

	value &= ~(AMD_CPPC_MAX_PERF_MASK | AMD_CPPC_MIN_PERF_MASK |
		   AMD_CPPC_DES_PERF_MASK | AMD_CPPC_EPP_PERF_MASK);
	value |= FIELD_PREP(AMD_CPPC_MAX_PERF_MASK, max_perf);
	value |= FIELD_PREP(AMD_CPPC_DES_PERF_MASK, des_perf);
	value |= FIELD_PREP(AMD_CPPC_MIN_PERF_MASK, min_perf);
	value |= FIELD_PREP(AMD_CPPC_EPP_PERF_MASK, epp);

	if (value == prev)
		return 0;

	if (fast_switch) {
		wrmsrl(MSR_AMD_CPPC_REQ, value);
		return 0;
	} else {
		int ret = wrmsrl_on_cpu(cpudata->cpu, MSR_AMD_CPPC_REQ, value);

		if (ret)
			return ret;
	}

	WRITE_ONCE(cpudata->cppc_req_cached, value);
	WRITE_ONCE(cpudata->epp_cached, epp);

	return 0;
}
```

共享内存实现：

```c
static int shmem_update_perf(struct amd_cpudata *cpudata, u8 min_perf,
			     u8 des_perf, u8 max_perf, u8 epp, bool fast_switch)
{
	struct cppc_perf_ctrls perf_ctrls;

	if (cppc_state == AMD_PSTATE_ACTIVE) {
		int ret = shmem_set_epp(cpudata, epp);

		if (ret)
			return ret;
	}

	perf_ctrls.max_perf = max_perf;
	perf_ctrls.min_perf = min_perf;
	perf_ctrls.desired_perf = des_perf;

	return cppc_set_perf(cpudata->cpu, &perf_ctrls);
}
```

主要函数为 cppc_set_perf。

在该函数中将值写入到寄存器时需要通过 pcc 总线，这里的写入过程分为两个阶段

Phase-I：并行写寄存器​​

​​检查PCC（Platform Communication Channel）​​
如果寄存器位于PCC空间（需通过平台总线访问），获取读锁 pcc_lock，确保多核并行写入时数据一致性。
标记待写入命令（pending_pcc_write_cmd = true），防止后续读操作干扰。
​​写入寄存器​​
调用 cpc_write() 写入 desired_perf（必写），min_perf 和 max_perf 仅在非零时写入。

​​Phase-II：提交PCC命令​​

​​尝试获取写锁​​
通过 down_write_trylock() 竞争写锁，​​只有一个CPU能成功进入Phase-II​​。
若失败，等待其他CPU完成命令提交（通过 wait_event 同步）。
​​触发硬件更新​​
成功获取写锁后，调用 send_pcc_cmd(CMD_WRITE) 向硬件发送调频指令。
更新状态（write_cmd_status）并释放锁。

​​锁的设计：

* pcc_lock（读）	允许多核同时进入Phase-I，并行写入寄存器，但阻塞PCC读操作。
* pcc_lock（写）	确保只有一个核进入Phase-II提交命令，避免重复触发硬件更新。

​​竞争处理​​：
若多个核同时调用此函数，最后一个完成Phase-I的核负责提交命令，其他核通过 wait_event 等待完成。

## AMD-pstate 与 ACPI-CPUFREQ

acpi 的兼容性更好，支持大多数的 AMD 平台，但在 AMD 处理器上仅提供 3 个 P 状态。但是对于一些业务场景下，三档调频并不够准确。而 AMD-pstate 基于 cppc 却能实现 0～255 的精准调频。

在 acpi-cpufreq 支持的大多数 AMD 平台上，平台固件提供的 ACPI 表用于 CPU 性能扩展，但在 AMD 处理器上仅提供 3 个 P 状态。但是，在现代 AMD APU 和 CPU 系列上，硬件根据 ACPI 协议提供协作处理器性能控制，并为 AMD 平台进行自定义。也就是说，精细和连续的频率范围，而不是传统的硬件 P 状态。amd-pstate 是支持大多数未来 AMD 平台上新 AMD P-States 机制的内核模块。AMD P-States 机制是 AMD 处理器上性能和能效更高的频率管理方法。