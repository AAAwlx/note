# bpftrace

bpftrace 是一个终端中集成bpf追踪工具，直接安装即可使用

依赖：clang llvm

## bpftrace - help

```shell
# bpftrace -h
USAGE:
    bpftrace [options] filename # 运行指定的 BPF 程序文件。
    bpftrace [options] - <stdin input> # 从标准输入运行 BPF 程序。
    bpftrace [options] -e 'program' # 在命令行中执行指定的 BPF 程序。

OPTIONS:
    -B MODE        output buffering mode ('full', 'none') # 控制输出的缓冲模式。full 为全缓冲，none 为无缓冲。
    -f FORMAT      output format ('text', 'json') # 指定输出的格式，可以是 'text' 或 'json'
    -o file        redirect bpftrace output to file # 将输出重定向到指定的文件
    -d             debug info dry run #分别用于调试信息的干运行（不会实际执行程序）和详细调试信息的干运行
    -dd            verbose debug info dry run
    -b             force BTF (BPF type format) # 强制进行 BTF（BPF 类型格式）的处理。
    -e 'program'   execute this program # 直接在命令行中指定一个 BPF 程序并执行。
    -h, --help     show this help message
    -I DIR         add the directory to the include search path
    --include FILE add an #include file before preprocessing
    -l [search]    list probes
    -p PID         enable USDT probes on PID #在指定进程 ID（PID）上启用 USDT（用户空间定义的追踪）探针。
    -c 'CMD'       run CMD and enable USDT probes on resulting process
    --usdt-file-activation
                   activate usdt semaphores based on file path
    --unsafe       allow unsafe builtin functions
    -v             verbose messages
    --info         Print information about kernel BPF support
    -k             emit a warning when a bpf helper returns an error (except read functions)
    -kk            check all bpf helper functions
    -V, --version  bpftrace version
    --no-warnings  disable all warning messages

ENVIRONMENT:
    BPFTRACE_STRLEN             [default: 64] bytes on BPF stack per str()
    BPFTRACE_NO_CPP_DEMANGLE    [default: 0] disable C++ symbol demangling
    BPFTRACE_MAP_KEYS_MAX       [default: 4096] max keys in a map
    BPFTRACE_CAT_BYTES_MAX      [default: 10k] maximum bytes read by cat builtin
    BPFTRACE_MAX_PROBES         [default: 512] max number of probes
    BPFTRACE_LOG_SIZE           [default: 1000000] log size in bytes
    BPFTRACE_PERF_RB_PAGES      [default: 64] pages per CPU to allocate for ring buffer
    BPFTRACE_NO_USER_SYMBOLS    [default: 0] disable user symbol resolution
    BPFTRACE_CACHE_USER_SYMBOLS [default: auto] enable user symbol cache
    BPFTRACE_VMLINUX            [default: none] vmlinux path used for kernel symbol resolution
    BPFTRACE_BTF                [default: none] BTF file

EXAMPLES:
bpftrace -l '*sleep*'
    list probes containing "sleep"
bpftrace -e 'kprobe:do_nanosleep { printf("PID %d sleeping...\n", pid); }'
    trace processes calling sleep
bpftrace -e 'tracepoint:raw_syscalls:sys_enter { @[comm] = count(); }'
    count syscalls by process name

```

bpftrace 脚本的格式

```shell
probes /filter/ { actions }
```

其中 probes 为挂载点类型+挂载点函数

## 文件打开

```sh
# bpftrace -e 'tracepoint:syscalls:sys_enter_openat { printf("%s %s\n", comm, str(args.filename)); }'
Attaching 1 probe...
snmp-pass /proc/cpuinfo
snmp-pass /proc/stat
snmpd /proc/net/dev
snmpd /proc/net/if_inet6
```

comm是内建变量，代表当前进程的名字。其它类似的变量还有pid和tid，分别表示进程标识和线程标识。
args是一个包含所有tracepoint参数的结构。这个结构是由bpftrace根据tracepoint信息自动生成的。这个结构的成员可以通过命令bpftrace -vl tracepoint:syscalls:sys_enter_openat找到。