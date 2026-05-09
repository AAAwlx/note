# ebpf 助手函数

eBPF（Extended Berkeley Packet Filter）助手函数是内核提供的一组函数，这些函数可以在eBPF程序中调用，帮助程序完成各种任务。eBPF助手函数覆盖了广泛的功能，包括网络数据包处理、map操作、时间管理等。以下是一些常用的eBPF助手函数及其简要说明：

常用的eBPF助手函数:

## bpf_map_lookup_elem

```c
void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)
```

功能: 在指定的 eBPF map 中查找元素。
返回值: 返回找到的元素指针，若未找到则返回 NULL。

## bpf_map_update_elem

```c
int bpf_map_update_elem(struct bpf_map *map, const void *key, const void *value, __u64 flags)
```

功能: 更新或插入元素到指定的eBPF地图中。
返回值: 成功返回0，失败返回负值。

## bpf_map_delete_elem

```c
int bpf_map_delete_elem(struct bpf_map *map, const void *key)
```

功能: 删除指定键对应的元素。
返回值: 成功返回0，失败返回负值。

## bpf_get_prandom_u32

```c
u32 bpf_get_prandom_u32(void)
```

功能: 生成一个32位伪随机数。
返回值: 32位伪随机数。

## bpf_ktime_get_ns

```c
 u64 bpf_ktime_get_ns(void)
```

功能: 获取从系统启动到现在的时间，单位为纳秒。
返回值: 当前时间，以纳秒为单位。

## bpf_trace_printk

```c
int bpf_trace_printk(const char *fmt, int fmt_size, ...)
```

功能: 向trace_pipe输出调试信息。
返回值: 成功返回0，失败返回负值。
注意: 使用此函数会增加系统开销，仅用于调试。

## bpf_get_current_pid_tgid

```c
u64 bpf_get_current_pid_tgid(void)
```

功能: 获取当前进程和线程的ID。
返回值: 低32位是PID，高32位是TGID。

## bpf_get_current_uid_gid

```c
u64 bpf_get_current_uid_gid(void)
```

功能: 获取当前进程的用户ID和组ID。
返回值: 低32位是UID，高32位是GID。

## bpf_perf_event_output

```c
 int bpf_perf_event_output(void *ctx, struct bpf_map *map, u64 index, void *data, u32 size)
```

功能: 向perf事件输出数据。
返回值: 成功返回0，失败返回负值。

## bpf_tail_call:

```c
int bpf_tail_call(void *ctx, struct bpf_map *prog_array_map, u32 index)
```

功能: 实现eBPF程序之间的尾调用。
返回值: 成功返回0，失败返回负值。
