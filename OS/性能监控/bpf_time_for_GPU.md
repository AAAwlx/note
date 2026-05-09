# 整体架构

```text
┌─────────────────────────────────────────────────────────────────┐
│                        应用层                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │
│  │ Examples │  │ Benchmark│  │  Tools   │  │    Daemon    │  │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                      运行时核心层                     │
│  ┌────────────────────────────────────────────────────────┐    │
│  │                    Runtime (运行时)                     │    │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐  │    │
│  │  │ Handler │ │ BPF Map │ │ Syscall │ │   Agent     │  │    │
│  │  │ Manager │ │         │ │ Server  │ │             │  │    │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────────┘  │    │
│  └────────────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────┤
│                      附加层                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐     │
│  │  Base    │ │  Frida   │ │ Syscall  │ │    NV/GPU    │     │
│  │  Attach  │ │  Uprobe  │ │  Trace   │ │    Attach    │     │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────┘     │
├─────────────────────────────────────────────────────────────────┤
│                      虚拟机层                         │
│  ┌────────────────────────────────────────────────────────┐    │
│  │                      VM (虚拟机)                        │    │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────────────┐   │    │
│  │  │ VM Core  │ │ LLVM JIT │ │   Compat Layer       │   │    │
│  │  └──────────┘ └──────────┘ │ (ubpf/llvm/compat)   │   │    │
│  │                              └──────────────────────┘   │    │
│  └────────────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────┤
│                      验证层                       │
│                     bpftime-verifier                          │
├─────────────────────────────────────────────────────────────────┤
│                      第三方依赖层                  │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌────────┐ ┌─────┐    │
│  │ spdlog│ |ubpf │ |Catch2│ |Frida │ | libbpf │ |LLVM │    │
│  └──────┘ └──────┘ └──────┘ └──────┘ └────────┘ └─────┘    │
└─────────────────────────────────────────────────────────────────┘
```

目录树结构与文件说明:

```text
bpftime/
├── 📁 attach/                    # 附加实现模块 - 处理 eBPF 程序的附加点
│   ├── base_attach_impl/         # 基础附加接口抽象
│   │   ├── base_attach_impl.hpp  # 附加接口基类定义
│   │   └── attach_private_data.hpp # 附加私有数据结构
│   │
│   ├── frida_uprobe_attach_impl/ # Frida 动态插桩实现 (用户空间探针)
│   │   ├── include/              # 公共头文件
│   │   │   ├── frida_uprobe_attach_impl.hpp  # Frida uprobe 附加实现
│   │   │   ├── frida_attach_entry.hpp         # 附加入口点定义
│   │   │   ├── frida_register_conversion.hpp  # 寄存器转换
│   │   │   └── frida_attach_utils.hpp         # 附加工具函数
│   │   ├── src/                 # 实现源文件
│   │   └── test/                # 单元测试
│   │
│   ├── syscall_trace_attach_impl/ # 系统调用追踪附加实现
│   │   ├── include/
│   │   │   ├── syscall_trace_attach_impl.hpp  # 系统调用追踪附加
│   │   │   └── syscall_table.hpp              # 系统调用表定义
│   │   └── src/
│   │
│   ├── nv_attach_impl/           # NVIDIA GPU 附加实现
│   │   ├── nv_attach_impl.hpp   # GPU 附加实现主文件
│   │   ├── nv_attach_utils.hpp  # GPU 附加工具
│   │   ├── ptx_compiler/        # PTX 编译器 (CUDA)
│   │   └── pass/                # LLVM Pass (PTX 处理)
│   │
│   ├── simple_attach_impl/       # 简单附加实现示例
│   └── text_segment_transformer/ # 代码段转换器
│
├── 📁 benchmark/                 # 性能测试套件
│   ├── gpu/                      # GPU 相关基准测试
│   │   ├── host/                # 主机端测试
│   │   ├── micro/               # 微基准测试
│   │   ├── nvbit/               # NVBit 工具测试
│   │   └── workload/            # 工作负载测试
│   ├── syscall/                  # 系统调用性能测试
│   ├── uprobe/                   # 用户探针性能测试
│   ├── fuse/                     # FUSE 文件系统测试
│   ├── redis-durability-tuning/  # Redis 持久化调优测试
│   └── ssl-nginx/                # SSL/Nginx 测试
│
├── 📁 bpftime-verifier/          # eBPF 验证器模块
│   ├── include/
│   │   └── bpftime-verifier.hpp # 验证器公共接口
│   ├── src/
│   │   ├── bpftime-verifier.cpp # 验证器实现
│   │   └── platform-impl.cpp    # 平台相关实现
│   ├── ebpf-verifier/           # PREVAIL 验证器
│   └── test/                     # 验证器测试
│
├── 📁 cmake/                      # CMake 构建脚本
│   ├── CompilerWarnings.cmake    # 编译器警告配置
│   ├── StandardSettings.cmake    # 标准构建设置
│   ├── frida.cmake               # Frida 集成
│   ├── libbpf.cmake              # libbpf 集成
│   ├── cuda.cmake                # CUDA 支持
│   └── rocm.cmake                # ROCm (AMD GPU) 支持
│
├── 📁 daemon/                     # 守护进程模块
│   ├── user/                     # 用户空间守护进程
│   │   ├── daemon.hpp           # 守护进程主类
│   │   ├── bpftime_driver.hpp   # bpftime 驱动
│   │   ├── handle_bpf_event.hpp # BPF 事件处理
│   │   └── main.cpp             # 守护进程入口
│   ├── kernel/                   # 内核空间组件
│   │   ├── bpf_defs.h           # BPF 定义
│   │   ├── bpf_event_ringbuf.h  # BPF 事件环形缓冲区
│   │   └── bpf_tracer_event.h   # 追踪事件
│   └── test/                     # 守护进程测试
│
├── 📁 example/                    # 示例程序
│   ├── minimal/                  # 最小化示例
│   │   ├── uprobe.bpf.c         # uprobe eBPF 程序
│   │   ├── syscall.bpf.c        # 系统调用追踪 eBPF
│   │   └── victim.c             # 被追踪的目标程序
│   ├── malloc/                   # 内存分配追踪示例
│   ├── error-inject/             # 错误注入示例
│   ├── tracing/                  # 追踪示例
│   │   └── bashreadline/        # Bash readline 追踪
│   ├── gpu/                      # GPU 相关示例
│   ├── hotpatch/                 # 热补丁示例
│   │   ├── vim/                 # Vim 热补丁
│   │   └── redis/               # Redis 热补丁
│   ├── bpf_features/             # BPF 特性演示
│   │   ├── tailcall_minimal/    # 尾调用示例
│   │   ├── bloom_filter_demo/   # 布隆过滤器演示
│   │   ├── lpm_trie_demo/       # LPM Trie 演示
│   │   └── queue_demo/          # 队列/栈演示
│   ├── xdp-counter/              # XDP 网络示例
│   ├── attach_implementation/    # 附加实现示例
│   │   ├── benchmark/           # 性能基准插件
│   │   │   ├── ebpf_controller/ # eBPF 控制器
│   │   │   ├── wasm_plugin/     # WASM 插件
│   │   │   └── luajit_plugin/   # LuaJIT 插件
│   │   ├── nginx_plugin/        # Nginx 插件
│   │   └── controller/          # 控制器示例
│   └── libbpftime_example/       # 库使用示例
│
├── 📁 runtime/                    # 核心运行时模块
│   ├── include/                  # 公共头文件
│   │   ├── bpftime.hpp          # 主运行时 API
│   │   ├── bpftime_shm.hpp      # 共享内存管理
│   │   ├── bpftime_prog.hpp     # eBPF 程序接口
│   │   ├── bpftime_ufunc.hpp    # 用户函数接口
│   │   ├── bpf_attach_ctx.hpp   # 附加上下文
│   │   ├── bpftime_config.hpp   # 配置管理
│   │   └── bpftime_helper_group.hpp # Helper 函数组
│   │
│   ├── src/                      # 核心实现
│   │   ├── bpftime_shm.cpp      # 共享内存实现
│   │   ├── bpftime_prog.cpp     # eBPF 程序实现
│   │   ├── bpf_helper.cpp       # BPF helper 函数 (38KB+)
│   │   ├── ufunc.cpp            # 用户函数支持
│   │   ├── bpftime_config.cpp   # 配置实现
│   │   ├── platform_utils.cpp   # 平台工具函数
│   │   │
│   │   ├── handler/             # Handler 模式实现
│   │   │   ├── handler_manager.hpp  # 对象管理器 (核心)
│   │   │   ├── handler_manager.cpp  # 管理器实现
│   │   │   ├── prog_handler.hpp     # 程序处理器
│   │   │   ├── map_handler.hpp      # Map 处理器
│   │   │   ├── link_handler.hpp     # 链接处理器
│   │   │   ├── perf_event_handler.hpp # 性能事件处理器
│   │   │   ├── epoll_handler.hpp    # Epoll 处理器
│   │   │   ├── btf_handler.hpp      # BTF 处理器
│   │   │   └── memfd_handler.hpp    # memfd 处理器
│   │   │
│   │   ├── bpf_map/             # Map 实现
│   │   │   ├── userspace/       # 用户空间 Map
│   │   │   │   ├── array_map.{hpp,cpp}      # 数组 Map
│   │   │   │   ├── fix_hash_map.{hpp,cpp}   # 固定大小哈希 Map
│   │   │   │   ├── var_hash_map.{hpp,cpp}   # 可变大小哈希 Map
│   │   │   │   ├── per_cpu_array_map.{hpp,cpp} # Per-CPU 数组
│   │   │   │   ├── per_cpu_hash_map.{hpp,cpp} # Per-CPU 哈希
│   │   │   │   ├── lru_var_hash_map.{hpp,cpp} # LRU 哈希
│   │   │   │   ├── ringbuf_map.{hpp,cpp}    # 环形缓冲区
│   │   │   │   ├── prog_array.{hpp,cpp}     # 程序数组 (尾调用)
│   │   │   │   ├── stack_trace_map.{hpp,cpp} # 栈追踪 Map
│   │   │   │   ├── lpm_trie_map.{hpp,cpp}   # LPM Trie
│   │   │   │   ├── queue.{hpp,cpp}          # 队列 Map
│   │   │   │   ├── bloom_filter.{hpp,cpp}   # 布隆过滤器
│   │   │   │   ├── map_in_maps.hpp          # Map-in-Map
│   │   │   │   └── perf_event_array_map.{hpp,cpp}
│   │   │   │
│   │   │   ├── shared/           # 内核-用户共享 Map
│   │   │   │   ├── array_map_kernel_user.{hpp,cpp}
│   │   │   │   ├── hash_map_kernel_user.{hpp,cpp}
│   │   │   │   ├── percpu_array_map_kernel_user.{hpp,cpp}
│   │   │   │   └── perf_event_array_kernel_user.{hpp,cpp}
│   │   │   │
│   │   │   └── gpu/              # GPU Map 实现
│   │   │       ├── nv_gpu_array_host_map.{hpp,cpp}
│   │   │       ├── nv_gpu_shared_array_map.{hpp,cpp}
│   │   │       ├── nv_gpu_shared_hash_map.{hpp,cpp}
│   │   │       ├── nv_gpu_per_thread_array_map.{hpp,cpp}
│   │   │       ├── nv_gpu_ringbuf_map.{hpp,cpp}
│   │   │       └── nv_gpu_gdrcopy.{hpp,cpp} # GDRCopy 支持
│   │   │
│   │   └── attach/              # 附加上下文
│   │       ├── bpf_attach_ctx.{hpp,cpp}
│   │       └── bpf_attach_ctx_cuda.cpp
│   │
│   ├── syscall-server/           # 系统调用服务器
│   │   ├── syscall_server_main.cpp   # 服务器入口
│   │   ├── syscall_context.{hpp,cpp} # 系统调用上下文
│   │   └── syscall_server_utils.{hpp,cpp}
│   │
│   ├── agent/                    # 注入代理
│   │   └── agent.cpp            # 代理实现
│   │
│   ├── extension/                # 扩展功能
│   │   ├── extension_helper.cpp # Helper 扩展
│   │   └── userspace_xdp.h      # 用户空间 XDP
│   │
│   ├── object/                   # BPF 对象处理
│   │   ├── bpftime_object.hpp   # 对象抽象
│   │   └── bpf_object.cpp       # BPF 对象加载
│   │
│   ├── test/                     # 集成测试
│   │   ├── bpf/                 # 测试用 eBPF 程序
│   │   │   ├── map.bpf.c        # Map 测试
│   │   │   ├── hash-map-test.bpf.c
│   │   │   ├── replace.bpf.c    # 替换测试
│   │   │   ├── patch.bpf.c      # 补丁测试
│   │   │   ├── ufunc.bpf.h      # 用户函数测试
│   │   │   └── btf-relo.bpf.c   # BTF 重定位测试
│   │   ├── include/             # 测试头文件
│   │   └── src/                 # 测试源文件
│   │
│   └── unit-test/                # 单元测试
│       ├── test_config.cpp      # 配置测试
│       ├── test_probe.cpp       # 探针测试
│       ├── maps/                # Map 单元测试
│       │   ├── test_bpftime_hash_map.cpp
│       │   ├── test_lru_hash_map.cpp
│       │   ├── test_per_cpu_hash.cpp
│       │   ├── test_queue_map.cpp
│       │   ├── test_stack_map.cpp
│       │   ├── test_lpm_trie_map.cpp
│       │   └── ...
│       ├── cuda/                # CUDA 相关测试
│       └── tailcall/            # 尾调用测试
│
├── 📁 third_party/                # 第三方依赖
│   ├── spdlog/                   # 日志库
│   ├── Catch2/                   # 测试框架
│   ├── ubpf/                     # uBPF 虚拟机
│   ├── argparse/                 # 参数解析
│   ├── bpftool/                  # BPF 工具 (含 libbpf)
│   └── vmlinux/                  # 内核头文件/类型定义
│
├── 📁 tools/                      # 工具程序
│   ├── cli/                      # 命令行工具 (bpftime CLI)
│   │   └── main.cpp             # bpftime 命令实现
│   ├── bpftimetool/              # bpftime 工具
│   │   └── main.cpp
│   └── aot/                      # AOT (Ahead-of-Time) 编译工具
│       ├── main.cpp
│       └── example/
│
├── 📁 vm/                         # 虚拟机模块
│   ├── vm-core/                  # 核心 VM 抽象
│   │   ├── include/
│   │   │   └── ebpf-vm.h        # eBPF VM 接口
│   │   └── src/
│   │       └── ebpf-vm.cpp      # VM 实现
│   │
│   ├── llvm-jit/                 # LLVM JIT/AOT 编译器
│   │   └── (子模块，外部 LLVM)
│   │
│   ├── compat/                   # 兼容层
│   │   ├── include/
│   │   │   ├── bpftime_vm_compat.hpp  # VM 兼容接口
│   │   │   └── ebpf_inst.h            # eBPF 指令定义
│   │   ├── ubpf-vm/            # uBPF VM 兼容
│   │   │   ├── compat_ubpf.{hpp,cpp}
│   │   └── llvm-vm/            # LLVM VM 兼容
│   │       └── compat_llvm.{hpp,cpp}
│   │
│   └── example/                  # VM 示例
│       └── bpf_progs.h          # 示例程序
│
├── CMakeLists.txt                # 主 CMake 构建配置
├── Makefile                      # Make 构建配置
├── README.md                     # 项目说明文档
├── installation.md               # 安装指南
├── usage.md                      # 使用指南
├── CONTRIBUTING.md               # 贡献指南
├── CLAUDE.md                     # Claude Code 项目指南
└── LICENSE                       # 许可证 (Apache 2.0)

```

## nv_attach_impl 架构

```text
┌─────────────────────────────────────────────────────────────────────┐
│                        nv_attach_impl                                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                    CUDA 运行时 Hook                           │   │
│  │  (使用 Frida 拦截 CUDA API 调用)                               │   │
│  │  ┌───────────────┬────────────────┬──────────────────────┐   │   │
│  │  │ cudaLaunch    │ cuGraphAdd     │ cudaMemcpyToSymbol   │   │   │
│  │  │ __cudaRegister│ __cudaRegister │ __cudaRegisterFatBin │   │   │
│  │  │ FatBinary     │ Function       │ End                  │   │   │
│  │  └───────────────┴────────────────┴──────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                    PTX 代码注入                                 │   │
│  │  (ptxpass - LLVM Pass 修改 PTX 指令)                           │   │
│  │  ┌────────────────────────────────────────────────────────┐  │   │
│  │  │  原始 PTX → 插入 eBPF 探针 → 修补后的 PTX                │  │   │
│  │  └────────────────────────────────────────────────────────┘  │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                    GPU Map 实现                                │   │
│  │  (主机- GPU 共享内存)                                          │   │
│  │  ┌──────────────┬────────────────┬──────────────────────┐    │   │
│  │  │ Shared HashMap│ Per-Thread     │ Ring Buffer          │    │   │
│  │  │              │ Array Map       │ (GPU → Host 通信)    │    │   │
│  │  └──────────────┴────────────────┴──────────────────────┘    │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

nv_attach_impl 通过修改 PTX (Parallel Thread Execution) 代码来实现 GPU kernel 事件追踪：


```text
原始流程：
┌──────────┐    ┌──────────┐    ┌──────────┐
│ CUDA程序  │ -> │ PTX代码   │ -> │ GPU执行   │
└──────────┘    └──────────┘    └──────────┘

带追踪流程：
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ CUDA程序  │ -> │ PTX代码   │ -> │ PTX Pass │ -> │ 注入探针   │
└──────────┘    └──────────┘    │ (修改)    │    └──────────┘
                               └──────────┘           │
                                                      ▼
                                               ┌──────────┐
                                               │ GPU执行   │
                                               │ + eBPF    │
                                               └──────────┘
```

实现步骤：

### 步骤 1: Hook CUDA 注册函数 (nv_attach_impl.cpp:279-332)

```c
// 拦截 CUDA FAT binary 注册
void *register_fatbin_addr = dlsym(RTLD_NEXT, "__cudaRegisterFatBinary");
register_hook(AttachedToFunction::RegisterFatbin, register_fatbin_addr);

// 拦截 CUDA 函数注册
void *register_function_addr = gum_module_find_export_by_name(
    nullptr, "__cudaRegisterFunction");
register_hook(AttachedToFunction::RegisterFunction, register_function_addr);
```

在CUDA程序启动时会先 hook 到 __cudaRegisterFatBinary 函数，在这个 hook 的处理中会把 CUDA 代码的 PTX 进行一个提取，然后在 CUDA Runtime 启动后，这里的会 hook 到 __cudaRegisterFunction ，在该函数中会建立 kernel 名与 PTX 的映射关系。

__cudaRegisterFatBinary: 注册 CUDA fatbinary 
* 触发时机: CUDA 程序启动时
* 拦截内容: FAT binary（包含编译好的 GPU 代码，通常是 PTX 或 CUBIN 格式）
* 作用:
  * 从参数中提取 PTX 代码 nv_attach_impl_frida_setup.cpp:139-163
  * 解析 FAT binary 格式
  * 保存到 fatbin_records 中供后续使用
__cudaRegisterFunction: 注册 GPU kernel 函数
* 触发时机: 注册每个 GPU kernel 函数时
* 拦截内容: kernel 函数的符号名和地址
* 作用:
  * 获取 kernel 函数名（如 matmul_kernel）和地址 nv_attach_impl_frida_setup.cpp:181-204
  * 将函数名与之前提取的 PTX 代码关联
  * 记录到 symbol_address_to_fatbin 映射表

```text
CUDA 程序启动
    ↓
CUDA Runtime 自动调用 __cudaRegisterFatBinary
    ↓
bpftime Hook 拦截 → 提取整个程序的 PTX 代码
    ↓
CUDA Runtime 为每个 kernel 调用 __cudaRegisterFunction  
    ↓
bpftime Hook 拦截 → 建立 kernel 名与 PTX 的映射关系
```


|阶段|Hook 的函数|作用|
|注册阶段|__cudaRegisterFatBinary|提取 PTX 代码|
|注册阶段|__cudaRegisterFunction|建立 kernel 名与 PTX 映射|
|执行阶段|cudaLaunchKernel|真正执行 GPU kernel|

### 步骤 2: 提取 PTX 代码 (nv_attach_impl.cpp:539-591)

```c
std::map<std::string, std::string> nv_attach_impl::extract_ptxs(
    std::vector<uint8_t> &&data_vec) {
    // 1. 将 fatbinary（二进制文件）写入临时文件
    // 2. 使用 cuobjdump 提取 PTX
    auto cuobjdump_cmd_line = std::string("cuobjdump --extract-ptx all ") 
                              + fatbin_path.string();
    // 3. 返回所有 PTX 文件的映射
    return all_ptx;
}
```

cuobjdump 是 NVIDIA CUDA 工具包中一个至关重要的命令行二进制分析工具。它可以被看作是 CUDA 领域的“拆解黑盒”工具，专门用于分析、反汇编和提取 CUDA 二进制文件（.cubin）、对象文件（.o）、静态库（.a）或可执行文件中的 GPU 代码。

1. cuobjdump 的主要功能提取内容： 从主机（Host）二进制文件中提取嵌入的 PTX（Parallel Thread Execution，中间代码）或 SASS（Streaming Assembler，硬件汇编代码）。反汇编 (SASS)： 将二进制的 SASS 代码反编译为人类可读的汇编指令，供开发者进行底层的性能分析和优化。查看 CUDA ELF 节： 检查 ELF（Executable and Linkable Format）格式文件中的节头、字符串表、重定位表等。分析架构特性： 检查编译出的二进制文件适用的 GPU 架构（计算能力，如 sm_75, sm_80 等）和 PTX 版本。

2. 核心输出内容
  * PTX 代码：.ptx 是一种中间表示形式，在运行时由驱动程序编译为特定 GPU 的机器码。 
  * SASS 代码：.sass 是针对特定 NVIDIA GPU 架构的实际汇编指令。ELF 头信息： .elf 文件的内部结构。

### 步骤 3: PTX 代码注入 (nv_attach_impl.cpp:593-720)

```text
原始 PTX                    PTX Pass                   修改后 PTX
─────────────────          ──────────────             ─────────────────
.func kernel_name(   ──→   查找目标     ──→   .func kernel_name(
    %param1)                    ↓                      %param1)
{                              找到入口                    {
    // 原始代码                    ↓                          call probe;
    ...                   插入 call 指令                    // 原始代码
}                              ↓                            }
                             完成
```

```c
std::optional<std::map<std::string, std::tuple<std::string, bool>>>
nv_attach_impl::hack_fatbin(std::map<std::string, std::string> all_ptx) {
    // 对每个 PTX 文件
    for (auto &[file_name, original_ptx] : all_ptx) {
        // 对每个要插桩的 kernel
        for (const auto &kernel : kernels) {
            // 调用 PTX Pass (独立进程)
            ptxpass::runtime_request::RuntimeRequest req;
            req.input.full_ptx = current_ptx;
            req.input.to_patch_kernel = kernel;
            req.set_ebpf_instructions(ebpf_inst_words);
            
            // PTX Pass 处理并返回修改后的 PTX
            resp.output_ptx = ...; // 包含 eBPF 探针的 PTX
        }
        // 包装 trampoline
        current_ptx = wrap_ptx_with_trampoline(current_ptx);
    }
}
```

PTX Pass 的工作 (外部独立进程)：

```c
# PTX Pass 伪代码
def ptxpass_process(input_json):
    ptx = input_json['full_ptx']
    kernel_name = input_json['to_patch_kernel']
    ebpf_instructions = input_json['ebpf_instructions']
    
    # 1. 找到 kernel 函数
    kernel_body = find_kernel_function(ptx, kernel_name)
    
    # 2. 将 eBPF 指令编译为 PTX
    ebpf_ptx = compile_ebpf_to_ptx(ebpf_instructions)
    
    # 3. 在 kernel 入口插入 eBPF 调用
    modified_ptx = kernel_body.replace(
        f".func {kernel_name}(",
        f".func {kernel_name}(\n    call ebpf_probe,\n"
    )
    
    return {"output_ptx": modified_ptx, "modified": True}
```

Pass = 对代码的一次遍历和转换

在该过程中完成了 原始 PTX → 插桩 Pass → 修改后 PTX 的多 Pass 编译流水线：

```text
┌─────────────────────────────────────────────────────────────┐
│                    PTX Pass 完整流程                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1️⃣ 编译 eBPF → PTX                                        │
│     ┌─────────────┐         ┌──────────────┐              │
│     │ eBPF 指令   │  ────→  │ eBPF PTX     │              │
│     │ (字节码)    │         │ __probe_func │              │
│     └─────────────┘         └──────────────┘              │
│                                                             │
│  2️⃣ 查找目标 kernel                                       │
│     在原始 PTX 中找到: .func kernel_name(...)              │
│                                                             │
│  3️⃣ 插入调用指令                                          │
│     在 kernel 入口添加: call __probe_func                  │
│                                                             │
│  4️⃣ 拼接输出                                              │
│     ┌──────────────┐         ┌──────────────┐            │
│     │ eBPF PTX     │  +      │ 原始 PTX     │            │
│     │ (新增代码)   │         │ (修改后)     │            │
│     └──────────────┘         └──────────────┘            │
│              ↓                      ↓                       │
│     ┌──────────────────────────────────────────┐           │
│     │           最终 PTX 文件                 │           │
│     │  .func __probe_func(...) { ... }       │           │
│     │  .func kernel_name(...) {              │           │
│     │      call __probe_func;  ← 插入的     │           │
│     │      // 原始代码...                   │           │
│     │  }                                    │           │
│     └──────────────────────────────────────────┘           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

步骤 4: Trampoline 包装 (trampoline_ptx.h)

```c
// Trampoline 模板
.visible .func _bpftime_trampoline_entry(
    .reg .pred p<NP>,
    .reg .b16 rs16,
    ...
    .reg .b64 rb<NR>,
    param64_t const_ptr    // eBPF 通信数据指针
) {
    // 1. 保存寄存器状态
    mov.b64 %save_r0, %r0;
    mov.b64 %save_r1, %r1;
    ...
    
    // 2. 调用 eBPF 程序
    {
        .reg .b64 tb_ptr;
        cvt.u64.u32 tb_ptr, const_ptr;
        call ebpf_main_function;
    }
    
    // 3. 恢复寄存器状态
    mov.b64 %r0, %save_r0;
    ...
    
    // 4. 返回原始函数
    ret;
}
```

执行时序图：

```text
用户代码                bpftime                     CUDA/GPU
   │                       │                           │
   │ bpftime load          │                           │
   │---------------------->│                           │
   │                       │                           │
   │ cudaLaunchKernel()    │                           │
   │---------------------->│                           │
   │                       │ Hook: 拦截启动            │
   │                       │-------------------------->│
   │                       │                           │
   │                       │ 1. 提取 PTX               │
   │                       │ 2. PTX Pass 注入 eBPF     │
   │                       │ 3. 编译为 cubin           │
   │                       │ 4. 加载修改后的 kernel    │
   │                       │                           │
   │                       │<--------------------------│
   │                       │                           │
   │                       │ GPU 执行 kernel           │
   │       ┌───────────────┼──────────────────────────>│
   │       │               │ 1. 触发 eBPF 探针         │
   │       │               │ 2. 读取/写入 GPU Map      │
   │       │               │ 3. 写入 ringbuf (可选)    │
   │       │               │                           │
   │       │               │<--------------------------│
   │       │               │                           │
   │       │ 读取 GPU Map  │                           │
   │       │<--------------│                           │
   │                       │                           │
```


|文件|作用|
|---|---|
|nv_attach_impl.cpp	|主实现，Frida Hook、PTX 处理流程|
|nv_attach_utils.hpp|工具函数|
|ptx_compiler/	|PTX 编译器封装|
|pass/ptxpass_core/	|PTX Pass 核心库 (注入逻辑)|
|trampoline_ptx.h	|PTX trampoline 模板|
|runtime/src/bpf_map/gpu/	|GPU Map 实现|
|nv_gpu_shared_hash_map.cpp	|共享哈希表|
|nv_gpu_ringbuf_map.cpp	|GPU → 主机通信|

## 可以监控的内容

bpftime for GPU 提供了以下专用 Helper 函数（通过 PTX 特殊寄存器实现）：

```
┌─────────────────────────────────────────────────────────────┐
│                   GPU Helper 函数列表                        │
├─────────────────────────────────────────────────────────────┤
│  ID  │ 函数名                    │ 返回值                   │
├──────┼──────────────────────────┼─────────────────────────┤
│  501 │ ebpf_puts                │ 调试输出                 │
│  502 │ bpf_get_globaltimer      │ GPU 全局时间戳 (ns)      │
│  503 │ bpf_get_block_idx        │ blockIdx (x,y,z)         │
│  504 │ bpf_get_block_dim        │ blockDim (x,y,z)         │
│  505 │ bpf_get_thread_idx       │ threadIdx (x,y,z)        │
│  507 │ cuda_exit                │ 退出 GPU                 │
│  508 │ get_grid_dim             │ gridDim (x,y,z)          │
│  509 │ get_sm_id                │ Streaming Multiprocessor ID │
│  510 │ get_warp_id              │ Warp ID (0-N)            │
│  511 │ get_lane_id              │ Lane ID (0-31)           │
└─────────────────────────────────────────────────────────────┘
```

### 可监控的指标分类

#### 1️⃣ 线程级别指标

| 指标 | 来源 | 说明 |
|------|------|------|
| **Thread 索引** | `bpf_get_thread_idx()` | threadIdx.x/y/z |
| **Block 索引** | `bpf_get_block_idx()` | blockIdx.x/y/z |
| **线程退出时间** | `bpf_get_globaltimer()` + kretprobe | 每个线程完成时间 |
| **线程执行次数** | Per-thread array map | 累积计数 |

#### 2️⃣ 硬件映射指标

| 指标 | 来源 | 说明 |
|------|------|------|
| **SM ID** | `bpf_get_sm_id()` | 分配到的流多处理器 |
| **Warp ID** | `bpf_get_warp_id()` | Warp 编号 |
| **Lane ID** | `bpf_get_lane_id()` | Warp 内位置 (0-31) |
| **SM 利用率** | SM 直方图 | 每个 SM 的线程数 |
| **Warp 分布** | Warp 直方图 | 每个 Warp 的线程数 |

#### 3️⃣ 时间/性能指标

| 指标 | 来源 | 说明 |
|------|------|------|
| **内核执行时间** | kprobe + kretprobe | entry/exit 时间差 |
| **启动延迟** | CPU uprobe + GPU kprobe | cudaLaunchKernel → GPU 执行 |
| **时间分布直方图** | 延迟分桶统计 | 100ns/1us/10us/... 桶 |

#### 4️⃣ 调用统计指标

| 指标 | 来源 | 说明 |
|------|------|------|
| **内核调用次数** | 计数器 map | 累积调用计数 |
| **进程级别统计** | PID keyed map | 按进程聚合 |

#### 5️⃣ 网格/Block 配置

| 指标 | 来源 | 说明 |
|------|------|------|
| **Grid 维度** | `get_grid_dim()` | gridDim.x/y/z |
| **Block 维度** | `bpf_get_block_dim()` | blockDim.x/y/z |

### GPU 示例目录说明

| 目录 | 功能 |
|------|------|
| **threadhist** | 线程执行直方图 — 统计每个 GPU 线程执行内核的次数，检测负载不均衡 |
| **kernel_trace** | 基础 kernel 追踪 — 捕获线程块/线程索引、GPU 时间戳 |
| **mem_trace** | 内存调用追踪 — 统计 CUDA 内核调用次数 |
| **launchlate** | 内核启动延迟分析 — 测量 `cudaLaunchKernel()` 到 GPU 实际执行的时间差 |
| **threadscheduling** | 线程调度映射 — 映射线程到 SM/Warp/Lane，可视化硬件分配 |
| **kernelretsnoop** | 线程退出时间戳 — 捕获每个线程完成执行的精确时间 |
| **cuda-counter** | 内核进入/退出探针 — 测量内核执行持续时间 |
| **cudagraph** | CUDA Graph 支持 — 追踪通过图启动的内核 |
| **gpu_shared_map** | GPU 共享 map 演示 — GPU 和主机共享数据 |
| **pytorch-test** | PyTorch 应用监控示例 |
| **faiss-test** | Faiss 向量检索库监控示例 |
| **cutlass** | CUTLASS GEMM 矩阵乘法监控 |

### 监控能力对比

| 监控维度 | NCU (硬件 PMU) | bpftime for GPU |
|----------|---------------|-----------------|
| **线程级时间** | ❌ | ✅ 纳秒级，每个线程 |
| **线程执行次数** | ❌ | ✅ 完全统计 |
| **SM/Warp/Lane** | ❌ 间接 | ✅ 直接读取 PTX 寄存器 |
| **启动延迟** | ❌ | ✅ CPU-GPU 时间差 |
| **内核执行时间** | ✅ 聚合 | ✅ 每个线程 |
| **时间分布** | ❌ | ✅ 直方图 |

### 硬件信息获取原理

bpftime 通过 PTX 公开的特殊寄存器获取硬件分配信息：

```ptx
// Helper 509: get_sm_id
mov.u32 %r1, %smid;        // ← 直接读取 PTX 寄存器

// Helper 510: get_warp_id
mov.u32 %r1, %warpid;      // ← 直接读取 PTX 寄存器

// Helper 511: get_lane_id
mov.u32 %r1, %laneid;     // ← 直接读取 PTX 寄存器
```

这些 PTX 特殊寄存器是 PTX 规范的一部分，不需要 NVIDIA 暴露底层硬件接口。
