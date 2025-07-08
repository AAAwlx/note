# ip 层的表及其作用

## 路由表

路由表用来决定数据包如何从源地址转发到目标地址。在路由表中会记录目标网络，以及目标网络对应的一些信息

![alt text](image.png)

* Destination：目标网络，当目标网络为default（0.0.0.0）时，表示这个是默认网关，所有数据都发到这个网关。
* Gateway：网关地址，即下一跳要转发到的路由器对应的ip，0.0.0.0 表示当前记录对应的 Destination 跟本机在同一个网段，不需要进行转发。
* Flags	
  * U - Up表示有效
  * G - Gateway表示连接路由，若无这个字段表示直连目的地址
  * H - Host表示目标是具体主机，而不是网段
  * R 恢复动态路由产生的表项
  * D 由路由的后台程序动态地安装
  * M 由路由的后台程序修改
  * ! 拒绝路由
* Metric：路由距离，到达指定网络所需的中转数，是大型局域网和广域网设置所必需的 （不在Linux内核中使用。）
* Ref：路由项引用次数 （不在Linux内核中使用。）
* Use：此路由项被路由软件查找的次数
* Iface：网卡名字，表示数据包会从哪个网卡上发出。

### linux路由的种类

主机路由：路由表中指向单个 IP 地址或主机名的路由记录，其 Flags 字段为 H。以下面的这条规则为例，说明这条路由规则只服务于发送到 10.0.0.10 的数据包。

```
Kernel IP routing table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
10.0.0.10       10.139.128.1    255.255.255.255 UGH   0      0        0 eth0
```

网络路由，主机可以到达的网络。如示例中目的IP的网络号为 9.0.0.0 都要转发到 21.6.180.1。

```
Kernel IP routing table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
9.0.0.0         21.6.180.1      255.0.0.0       UG    0      0        0 eth1
```
默认路由，当目标主机的 IP 地址或网络不在路由表中时，数据包就被发送到默认路由（默认网关）上。默认路由的 Destination 是 default 或 0.0.0.0。

```
Kernel IP routing table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
0.0.0.0         21.6.180.1      0.0.0.0         UG    0      0        0 eth1
```

### 路由表的配置

**静态路由**

​静态路由​​无协议开销​​，不会占用带宽和计算资源。但需要管理员​手动配置​​无自动更新机制。使用于​​简单稳定的小型网络或固定路径。

**动态路由**

​动态路由的优点是可以​自动学习​​，路由器通过协议交换路由信息，实时更新路由表。​自适应拓扑​​，自动选择最优路径，支持故障切换。

​​协议分类​​：
​​内部网关协议（IGP）​​：用于同一自治系统内部，如：​RIP​​、​OSPF​​。
​​外部网关协议（EGP）​​：用于不同AS间，如 ​​BGP​​。

### 路由涉及的内核函数


## 邻居表
邻居表维护 IP 地址与 MAC 地址的映射（IPv4 用 ARP，IPv6 用 NDP）。

![alt text](image-1.png)

* Address​​：目标设备的 ​​IPv4 或 IPv6 地址
* HWtype：网络接口的​​硬件类型，图中为
* HWaddress：目标设备的 ​​MAC 地址​​即物理地址。
* Flags
  * C	​​Complete​​	条目有效且已确认（收到过ARP响应）
  * M	​​Manual​​	静态配置（手动 arp -s 绑定）
  * P	​​Publish​​	本机可响应此IP的ARP请求（代理ARP）
  * S	​​Stale​​	条目可能失效（需重新验证）
  * R	​​Router​​	目标是一台路由器（IPv6 NDP专用）
  * D	​​Dynamic​​	动态学习（非静态）

Mask：表示子网掩码，仅在某些系统​​中显示，这里在 Linux 上就没有显示
Iface：网卡名字，表示数据包会从哪个网卡上发出。

### 邻居表的配置

邻居表中的表项可以通过静态的配置或者动态的学习。

**静态配置**

静态配置管理员可以手动添加ARP条目。

```sh
sudo arp -s 192.168.1.200 00:1a:2b:3c:4d:02
```
静态条目的特点​​是​永久有效​​：除非手动删除或重启网络服务，​Flags 为 M​。
**动态学习**

当主机需要与目标IP通信但不知道其MAC地址时，会触发ARP协议自动学习。

当主机A需要和B进行通信时，如果但ARP表中无主机B的MAC地址。主机A会发送 ​ARP 请求广播包。​当​交换机收到广播帧后，会将其​​泛洪​到除接收端口外的所有端口。主机 B 收到广播后，回复 ​ARP 响应包​。其他主机则会将这个包丢弃。当主机 A 收到主机 B 的响应消息会 MAC。如果主机 A 和主机 B 不在一个子网下那么学到的则是网关的 MAC 地址。

​动态学习的特点
* Flags C 表示动态学习（Complete）。
* ​​老化机制​​：条目默认保留 ​​15-30分钟​​（可配置），超时后删除。
* ​广播风暴控制​​：ARP请求会广播到整个局域网，但频率有限制。

### 内核函数

## Netfilter

Netfilter 表​​ 是 Linux 内核中用于实现 ​​防火墙和 ​网络地址转换的核心机制，它通过一系列规则表和链来控制数据包的流动。这里会用 iptable 命令来进行操作。

在 Netfilter 有以下的表：

filter table：过滤（放行/拒绝）
filter table 是最常用的 table 之一，用于判断是否允许一个包通过。

​​pkts​​	​​匹配该规则的数据包数量​​	100	表示已有 100 个数据包命中此规则。若为 0，说明近期无流量匹配。
​​bytes​​	​​匹配该规则的数据包总字节数​​	5000	表示命中规则的数据包累计大小（单位：字节）。
​​target​​	​​规则动作​​	ACCEPT、DROP、REJECT	对匹配流量的处理方式：
- ACCEPT：允许
- DROP：静默丢弃
- REJECT：拒绝并返回错误。

在防火墙领域，这通常称作“过滤”包（”filtering” packets）。这个 table 提供了防火墙 的一些常见功能。

nat table：网络地址转换
nat table 用于实现网络地址转换规则。

当包进入协议栈的时候，这些规则决定是否以及如何修改包的源/目的地址，以改变包被 路由时的行为。nat table 通常用于将包路由到无法直接访问的网络。

mangle table：修改 IP 头
mangle（修正）table 用于修改包的 IP 头。

例如，可以修改包的 TTL，增加或减少包可以经过的跳数。

这个 table 还可以对包打只在内核内有效的“标记”（internal kernel “mark”），后 续的 table 或工具处理的时候可以用到这些标记。标记不会修改包本身，只是在包的内核 表示上做标记。

raw table：conntrack 相关
iptables 防火墙是有状态的：对每个包进行判断的时候是依赖已经判断过的包。

建立在 netfilter 之上的连接跟踪（connection tracking）特性使得 iptables 将包 看作已有的连接或会话的一部分，而不是一个由独立、不相关的包组成的流。 数据包到达网络接口之后很快就会有连接跟踪逻辑判断。

raw table 定义的功能非常有限，其唯一目的就是提供一个让包绕过连接跟踪的框架。

security table：打 SELinux 标记
security table 的作用是给包打上 SELinux 标记，以此影响 SELinux 或其他可以解读 SELinux 安全上下文的系统处理包的行为。这些标记可以基于单个包，也可以基于连接。

​​prot​​	​​协议类型​​	tcp、udp、icmp、all	匹配的 IP 协议（all 表示任意协议）。
​​opt​​	​​额外选项​​	--dport 80	扩展匹配条件（如端口、TCP标志等）。
​​in​​	​​输入接口​​（数据包进入的网卡）	eth0、docker0	匹配从指定网卡进入的流量（* 表示任意接口）。
​​out​​	​​输出接口​​（数据包离开的网卡）	eth1、!eth0	匹配从指定网卡离开的流量（! 表示排除）。
​​source​​	​​源 IP 地址/网段​​	192.168.1.100、0.0.0.0/0	匹配来自指定 IP 的流量（0.0.0.0/0 表示任意地址）。
​​destination​​	​​目标 IP 地址/网段​​	10.0.0.1、0.0.0.0/0	匹配发往指定 IP 的流量。

每个表有五个默认的链，这个链代表了一些行为。

除了默认的链之外，用户也可以自己定义链。

连接跟踪
使用 Conntrack 指令来查看。
​​作用​​：跟踪网络连接状态（用于 NAT、状态防火墙）。

![alt text](image-2.png)

​​路由表​​：struct fib_table (在 net/ipv4/fib_frontend.c)
​​邻居表​​：struct neigh_table (在 net/core/neighbour.c)
​​Netfilter​​：struct nf_table (在 net/netfilter/core.c)
​​连接跟踪​​：struct nf_conn (在 net/netfilter/nf_conntrack_core.c)