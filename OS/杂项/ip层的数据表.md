IP（Internet Protocol）相关的表在网络协议栈中扮演重要角色，尤其是在Linux系统中。以下是常见的IP相关表及其用途的总结：

1. NAT 表（网络地址转换）

• 作用：用于修改IP数据包的源/目标地址（SNAT/DNAT），实现私有网络与公网的地址转换。

• 常见工具：iptables/nftables。

• 子表：

  • PREROUTING：DNAT（目标地址转换，如端口转发）。

  • POSTROUTING：SNAT（源地址转换，如共享上网）。

  • OUTPUT：本地进程发出的数据包NAT。

2. 路由表（Routing Table）

• 作用：决定数据包从哪个接口发送或下一跳地址。

• 查看命令：ip route show 或 route -n。

• 类型：

  • 主表（默认表，ID 254，名为main）。

  • 自定义表：通过策略路由（Policy Routing）管理。

  • 特殊表：

    ◦ local：本地地址路由（如127.0.0.1）。

    ◦ default：默认网关。

3. ARP 表（地址解析协议表）

• 作用：存储IP地址与MAC地址的映射关系。

• 查看命令：arp -an 或 ip neigh show。

4. Conntrack 表（连接跟踪表）

• 作用：跟踪网络连接状态（如TCP/UDP会话），是NAT和防火墙的基础。

• 查看命令：conntrack -L 或 cat /proc/net/nf_conntrack。

5. IPsec 安全关联表（Security Association Table）

• 作用：存储IPsec VPN的加密密钥和参数。

• 工具：ip xfrm state。

6. 策略路由表（Policy Routing Tables）

• 作用：基于规则（如源IP、服务类型）选择不同的路由表。

• 工具：ip rule list + ip route show table <ID>。

7. 防火墙表（iptables/nftables）

• 作用：过滤、修改或丢弃IP数据包。

• 常见表：

  • filter：默认过滤表（INPUT/OUTPUT/FORWARD链）。

  • mangle：修改数据包（如TTL、QoS）。

  • raw：连接跟踪前处理（如NOTRACK）。

8. 多播路由表（Multicast Routing）

• 作用：管理组播（Multicast）数据包的路由。

• 工具：smcroute 或 pimd。

9. 邻居发现表（IPv6 ND Table）

• 作用：IPv6中替代ARP表，存储IP-MAC映射。

• 查看命令：ip -6 neigh show。

10. FIB（Forwarding Information Base）

• 作用：内核快速转发的路由缓存，由路由表生成。

关键工具

• Linux：ip、iptables/nftables、ss、conntrack。

• Windows：route print（路由表）、arp -a（ARP表）。

根据具体场景（如NAT、防火墙、路由优化），这些表可能需要联合配置。例如，NAT需要依赖conntrack表跟踪连接，而策略路由需要同时配置ip rule和自定义路由表。