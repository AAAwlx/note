# DHCP协议

## 1. DHCP 是什么

* **作用**：自动给局域网内的主机分配网络配置参数，避免手工配置 IP。
* **工作层次**：属于 **应用层协议**，基于 **UDP** 进行通信。

  * 服务器端口：**UDP 67**
  * 客户端端口：**UDP 68**

主要分配内容包括：

* IP 地址
* 子网掩码
* 默认网关
* DNS 服务器
* 租约时间（Lease Time）

---

## 2. DHCP 工作流程（四步握手 DORA）

DHCP 的核心过程就是 **DORA 流程**（Discover → Offer → Request → Acknowledge）：

1. **Discover（发现）**

   * 客户端第一次上线，没有 IP，使用广播 **DHCPDISCOVER** 报文
   * 目标 IP：`255.255.255.255`
   * 作用：告诉网络里的 DHCP 服务器：“我需要一个 IP”

2. **Offer（提供）**

   * DHCP 服务器收到 Discover 后，挑选一个可用 IP，发 **DHCPOFFER**
   * 也通过广播发出，里面包含建议分配的 IP、子网掩码、网关、租约时间

3. **Request（请求）**

   * 客户端收到多个 Offer 后，只选择一个（通常第一个），广播 **DHCPREQUEST** 表示接受
   * 同时告诉其他服务器：“我已经选了某个 IP，你们不用再给我了”

4. **Acknowledge（确认）**

   * 被选中的 DHCP 服务器回复 **DHCPACK**，正式分配 IP 给客户端
   * 客户端获得 IP，配置完成，可以正常通信

如果服务器拒绝，可能会发 **DHCPNAK**（Negative Acknowledge），客户端就需要重新请求。

---

## 3. DHCP 租约机制

* 分配的 IP 不是永久的，而是有 **租约时间（Lease Time）**
* 客户端在租期过半时，会主动向 DHCP 服务器发送续租请求（DHCPREQUEST）
* 如果成功续租，DHCPACK 更新租约；如果失败，就需要重新走 DORA 流程

---

## 4. DHCP 的报文格式（简要）

DHCP 报文是基于 **BOOTP 报文扩展**的，关键字段有：

* **op**：报文类型（1=请求，2=应答）
* **chaddr**：客户端硬件地址（MAC 地址）
* **yiaddr**：服务器为客户端分配的 IP（your IP）
* **siaddr**：服务器 IP
* **xid**：事务 ID，用来匹配请求和应答
* **options**：选项字段（里面包含租约时间、DNS、网关等信息）

---

## 5. 使用场景

* 家庭/办公室路由器：路由器就是 DHCP 服务器，自动给 PC、手机分配 IP
* 大型企业网络：集中 DHCP 服务器，统一管理 IP
* 数据中心/云计算环境：虚拟机、容器需要动态 IP

---

✅ **一句话总结**：
DHCP 让设备即插即用，不需要手动配置 IP，通过 DORA 四步完成动态地址分配，并支持租约续期机制。

---

要不要我帮你画一个 **DHCP 报文交互时序图**（Discover → Offer → Request → Ack）？那样会更直观。
