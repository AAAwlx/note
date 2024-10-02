# Namespace

Linux 中的 `namespace` 主要用于实现资源隔离和限制，具体作用包括：

1. **资源隔离**：通过将不同的进程放入不同的命名空间中，可以实现网络、进程、用户、挂载等资源的隔离，使得各命名空间内的资源相互独立。

2. **安全性**：命名空间可以增强系统安全性，通过限制进程对系统资源的访问，减少潜在的攻击面。

3. **轻量级虚拟化**：命名空间为容器技术（如 Docker）提供了基础，使得多个应用可以在同一主机上运行而不会互相干扰。

4. **进程管理**：通过 PID 命名空间，可以对进程进行隔离，允许不同的进程拥有相同的 PID，从而实现更灵活的进程管理。

5. **简化环境配置**：命名空间可以为应用程序提供独立的运行环境，使得应用程序在不同环境中可以使用相同的配置而不会相互影响。

Namespace 会在进程之间进行隔离，造成进程以为自己独占

## Namespace 中的核心结构体

`nsproxy`结构被记录在进程的任务控制块task_struct中用以进程间的隔离，只有同属于一个namespace下的进程互相之间才是可见的。

```c
struct nsproxy {
	atomic_t count;//原子计数器，用于管理结构的引用计数
	struct uts_namespace *uts_ns;//指向 UTS 命名空间的指针，存储系统标识符信息。
	struct ipc_namespace *ipc_ns;//指向 IPC 命名空间的指针，管理进程间通信资源。
	struct mnt_namespace *mnt_ns;//指向挂载命名空间的指针，管理文件系统的挂载点。
	struct pid_namespace *pid_ns;//指向 PID 命名空间的指针，管理进程 ID 的命名空间。
	struct net 	     *net_ns;//指向网络命名空间的指针，管理网络资源和设置。
};
```

![Alt text](namespace.png)

## 根目录的切换pivot_root

该系统调用会更改 nsproxy 结构中 mnt_namespace 指向的root目录在docker初始化时会将busybox文件进行解压

```c

```

在宏 `user_path_dir` 中调用了 user_path_at ，而在 user_path_at 则调用 vfs层提供的路径解析函数对路径进行解析，最终将检索到的路径结构地址赋值给参数path。通过`user_path_dir`来获取旧的路径。

```c
user_path_dir(name, path) \
	user_path_at(AT_FDCWD, name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, path)
```

