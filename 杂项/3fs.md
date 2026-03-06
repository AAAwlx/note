
项目架构：

```mermaid

graph TD
    subgraph 客户端层
        A[应用程序] -->|标准文件API| B[FUSE客户端\nhf3fs_fuse_main]
        A -->|高性能接口| C[USRBIO客户端\nbypass内核]
    end

    subgraph 内核态
        B -->|VFS调用| D[内核文件系统]
        C -->|直接DMA| E[存储设备驱动]
    end

    subgraph 用户态服务
        D --> F[元数据服务\nmeta_main]
        E --> G[存储引擎\nchunk_engine]
        F --> H[FoundationDB]
        G --> I[SSD存储池]
    end

    style A fill:#c9daf8,stroke:#333
    style B fill:#d9ead3,stroke:#333
    style C fill:#d9ead3,stroke:#333
    style D fill:#fce5cd,stroke:#333
    style E fill:#fce5cd,stroke:#333
    style F fill:#fff2cc,stroke:#333
    style G fill:#fff2cc,stroke:#333
    style H fill:#d0e0e3,stroke:#333
    style I fill:#d0e0e3,stroke:#333
```
FUSE 客户端 ：实现 FUSE（用户空间文件系统）接口，允许应用程序使用 3FS 作为具有熟悉的 POSIX 语义的标准文件系统挂载点。

USRBIO Native Client ：一款用于零拷贝 I/O 操作的高性能 API，可绕过 FUSE 的一些限制。它使用共享内存区域 (Iov) 和环形缓冲区 (Ior) 来实现高效的数据传输。

```mermaid
sequenceDiagram
    应用->>+FUSE: 文件读写请求
    FUSE->>+VFS: 系统调用转换
    VFS->>+元数据服务: 查询inode
    元数据服务-->>-VFS: 返回chunk位置
    VFS->>+存储引擎: 数据块读写
    存储引擎-->>-应用: 返回结果
```

```mermaid
sequenceDiagram
    participant C as 客户端
    participant S as 应用服务
    participant R as Redis
    participant DB as MySQL
    participant MQ as 消息队列

    Note over C, S: 领取请求阶段
    C->>S: 请求领取优惠券(模板ID)
    S->>R: DECR stock:template_id (原子扣减预库存)
    
    alt 库存 >= 0
        Note right of R: 预扣成功，剩余库存n
        S->>DB: 在优惠券实例表生成记录<br>status='未使用'
        S->>DB: 在用户卡包表关联用户与券ID
        S->>C: 返回领取成功
        
        Note over S, MQ: 异步同步阶段
        S->>MQ: 发送异步消息(模板ID, 操作量-1)
        MQ->>DB: 消费消息，更新模板表库存<br>SET stock = stock - 1
       
    else 库存 < 0
        Note right of R: 预扣失败，库存不足
        S->>C: 返回“已抢光”
    end
```

```mermaid
sequenceDiagram
    participant U as 用户
    participant S as 服务端
    participant R as Redis
    participant M as MySQL
    participant MQ as RocketMQ
    participant Job as 补偿Job

    Note over U, S: 1. 领取优惠券请求
    U->>S: 点击领取
    S->>R: 执行lua脚本：<br>1. 检查库存 & 个人限领<br>2. 扣减库存 (DECR)
    Note right of R: 原子操作，防超发

    alt Redis操作成功
        S-->>U: 返回“领取成功”
    else Redis操作失败
        S-->>U: 返回“库存不足”等错误
    end

    Note over S, MQ: 2. 异步落库 (最终一致性)
    S->>MQ: 发送异步消息<br>（包含uid, coupon_id等）

    Note over MQ, M: 3. 消费者处理消息
    MQ->>M: 消费消息：<br>1. 写用户券表<br>2. 核减MySQL库存 (DECR)

    alt MySQL操作成功
        M-->>MQ: ACK，消息消费成功
    else MySQL操作失败（如网络抖动）
        M-->>MQ: NACK，消息重试
    end

    Note over M, Job: 4. 处理最终失败（如库存不足）
    loop 重试达上限后
        MQ->>Job: 将失败消息转入死信队列
        Job->>M: 查询当前库存记录
        alt 库存记录存在 (最终成功)
            Job->>R: 无需回滚Redis库存<br>(已最终一致)
        else 库存不足等原因 (最终失败)
            Job->>R: 执行回滚：<br>INCR Redis库存
            Job->>Job: 记录日志、告警
        end
    end
```
```mermaid
sequenceDiagram
    participant 客户端
    participant 应用服务
    participant MySQL
    participant RocketMQ
    participant 过期处理服务

    Note over 应用服务, MySQL: 领取主流程成功完成
    应用服务->>MySQL: 在用户券表插入记录<br/>status='未使用'，<br/>并记录expiry_time
    MySQL-->>应用服务: 写入成功

    Note left of 应用服务: 过期处理流程开始
    应用服务->>RocketMQ: 发送延迟消息<br/>(消息体:券ID, 延迟等级=过期时间间隔)
    Note right of RocketMQ: 消息进入延迟队列，<br/>等待指定时间后<br/>才可被消费
    RocketMQ-->>应用服务: 发送确认

    Note over RocketMQ, 过期处理服务: 延迟时间到（例如72小时后）
    RocketMQ->>过期处理服务: 投递延迟消息
    过期处理服务->>MySQL: 根据券ID查询状态
    MySQL-->>过期处理服务: 返回状态status

    alt 状态为 '未使用'
        过期处理服务->>MySQL: 执行更新: status='已过期'
        MySQL-->>过期处理服务: 更新成功
        过期处理服务->>RocketMQ: 消费成功(ACK)
    else 状态非 '未使用' (如'已使用')
        过期处理服务->>RocketMQ: 消费成功(ACK)<br/>(无需处理，直接确认)
    end
```
```mermaid

sequenceDiagram
    participant 定时调度器
    participant 统计归档服务
    participant MySQL
    participant HDFS

    Note over 定时调度器, 统计归档服务: 1. 触发统计任务（如每日凌晨1点）
    定时调度器->>统计归档服务: 触发任务执行

    Note over 统计归档服务, MySQL: 2. 查询与统计
    统计归档服务->>MySQL: 执行复杂查询：<br/>SELECT COUNT(*), batch_id, status<br/>FROM user_coupons<br/>WHERE expiry_date BETWEEN ? AND ?<br/>GROUP BY batch_id, status
    MySQL-->>统计归档服务: 返回统计结果集

    Note over 统计归档服务, HDFS: 3. 写入分布式存储
    统计归档服务->>HDFS: 写入统计结果文件<br/>（如：/coupon_stats/date=20240531/part-00001.parquet）
    HDFS-->>统计归档服务: 写入成功确认

    Note over 统计归档服务, 定时调度器: 4. 任务完成
    统计归档服务->>定时调度器: 任务完成，返回成功状态
```

```mermaid
sequenceDiagram
    participant 消息队列 as 消息队列 (RocketMQ)
    participant 过期处理服务
    participant MySQL
    participant HDFS

    Note over 消息队列, 过期处理服务: 线上过期处理流程（实时）
    消息队列->>过期处理服务: 投递延迟消息（券ID）
    过期处理服务->>MySQL: 根据券ID查询并更新状态->'已过期'
    MySQL-->>过期处理服务: 更新成功
    过期处理服务->>消息队列: ACK确认消费

    Note over 过期处理服务, HDFS: 离线统计归档流程（定时）
    loop 定时触发（如每日凌晨）
        过期处理服务->>MySQL: 执行复杂查询<br>SELECT batch_id, COUNT(*) <br>FROM coupons <br>WHERE status='已过期' <br>AND update_time BETWEEN ? AND ? <br>GROUP BY batch_id
        MySQL-->>过期处理服务: 返回统计结果集
        过期处理服务->>HDFS: 写入统计结果文件<br>（/coupon_expired_stats/date=20240531/data.parquet）
    end
```
```mermaid
flowchart TD
    subgraph A [运营侧]
        direction TB
        O[运营人员]
        O -- 创建/管理 --> T[优惠券模板服务<br>Coupon Template Service]
        O -- 查看数据/分析 --> D[运营分析后台<br>Operation Portal]
    end

    subgraph B [发放流程]
        direction LR
        T -- 提供模板 --> Dis[优惠券分发服务<br>Distribution Service]
        U -- 领取请求 --> Dis
        Dis -- 发放/推送 --> UC
    end

    subgraph C [用户侧]
        direction TB
        U[用户]
        U -- 查看 --> UC[用户优惠券服务<br>User Coupon Service]
    end

    subgraph E [核销流程]
        direction LR
        UC -- 查询可用券 --> Cal[计算引擎/核销服务<br>Calculation Engine]
        Cal -- 校验规则/计算优惠 --> Order[订单系统]
        Order -- 核销请求 --> Cal
        Cal -- 核销结果 --> UC
    end

    subgraph F [支撑服务]
        F_R[风控服务<br>Risk Control]
        F_N[通知服务<br>Notification Service]
        
        Dis -- 校验风控规则 --> F_R
        Dis -- 发放成功通知 --> F_N
        F_N -- 推送/短信 --> U
    end

    A -- 模板基础 --> B
    B -- 用户持有券 --> C
    C -- 使用 --> E
    F -.-> B
```

```mermaid
sequenceDiagram
    participant User as 用户/商户
    participant Order as 订单系统
    participant Engine as 计算引擎<br>(核销服务)
    participant UC as 用户优惠券服务
    participant DB as 数据库
    participant Risk as 风控系统
    participant MQ as 消息队列(MQ)

    User->>Order: 提交订单，选择优惠券
    Order->>Engine: 请求核销优惠券(传递订单及券信息)
    Engine->>Risk: 校验风险规则(防刷)
    Risk-->>Engine: 风控校验结果
    Engine->>UC: 查询该券详细信息及状态
    UC-->>Engine: 返回券信息(规则快照、状态等)
    Note over Engine: 核心校验: 有效期、门槛、<br>商品品类、叠加规则等
    alt 校验成功
        Engine->>DB: 锁定券状态(防止并发使用)
        Engine-->>Order: 返回核销成功及优惠金额
        Order->>User: 订单创建成功，等待支付
        User->>Order: 完成支付
        Order->>Engine: 确认最终核销指令
        Engine->>DB: 正式更新券状态为“已使用”
        Engine->>MQ: 发送核销成功消息
        MQ->>Risk: 更新风控数据(核销计数)
        MQ->>UC: 更新用户券列表状态
        MQ->>Monitor: 发送数据供监控/分析
    else 校验失败
        Engine-->>Order: 返回核销失败及原因
        Order-->>User: 提示优惠券不可用
    end

```

```mermaid
sequenceDiagram
    participant User as 用户/商户
    participant Order as 订单系统
    participant Engine as 计算引擎<br>(核销服务)
    participant UC as 用户优惠券服务
    participant DB as 数据库
    participant Risk as 风控系统
    participant MQ as 消息队列(MQ)

    User->>Order: 提交订单，选择优惠券
    Order->>Engine: 请求核销优惠券(传递订单及券信息)
    Engine->>Risk: 校验风险规则(防刷)
    Risk-->>Engine: 风控校验结果
    Engine->>UC: 查询该券详细信息及状态
    UC-->>Engine: 返回券信息(规则快照、状态等)
    Note over Engine: 核心校验: 有效期、门槛、<br>商品品类、叠加规则等
    alt 校验成功
        Engine->>DB: 锁定券状态(防止并发使用)
        Engine-->>Order: 返回核销成功及优惠金额
        Order->>User: 订单创建成功，等待支付
        User->>Order: 完成支付
        Order->>Engine: 确认最终核销指令
        Engine->>DB: 正式更新券状态为“已使用”
        Engine->>MQ: 发送核销成功消息
        MQ->>Risk: 更新风控数据(核销计数)
        MQ->>UC: 更新用户券列表状态
        MQ->>Monitor: 发送数据供监控/分析

        Note left of User: 用户取消订单流程
        User->>Order: 请求取消订单(已支付)
        Order->>Engine: 请求回滚优惠券(订单号, 券ID)
        Engine->>DB: 查询核销记录并校验
        alt 符合回滚条件(如未超时)
            Engine->>DB: 更新券状态为“未使用”
            Engine->>MQ: 发送回滚成功消息
            MQ->>Risk: 更新风控数据(回滚计数)
            MQ->>UC: 更新用户券列表状态
            MQ->>Monitor: 发送回滚数据供分析
            Engine-->>Order: 回滚成功
            Order-->>User: 取消成功，券已退回
        else 不符合回滚条件(如券已过期)
            Engine-->>Order: 回滚失败，券不可用
            Order-->>User: 取消成功，但券已过期不退
        end
    else 校验失败
        Engine-->>Order: 返回核销失败及原因
        Order-->>User: 提示优惠券不可用
    end
```

```mermaid
sequenceDiagram
    participant Client as 客户端
    participant App as 应用服务
    participant Redis as Redis
    participant DB as MySQL
    participant MQ as 消息队列

    Note over Client, App: 领取请求阶段
    Client->>App: 请求领取优惠券(模板ID)
    App->>Redis: DECR 库存Key (预扣库存)
    Note right of Redis: 原子操作<br>返回剩余库存n

    alt 预扣成功，剩余库存 n >= 0
        App->>DB: 开启事务
        App->>DB: 在优惠券实例表生成记录(status='未使用')
        App->>DB: 在用户卡包表插入关联(用户ID, 券ID)
        App->>DB: 提交事务
        alt 事务执行成功 (领券成功)
            App-->>Client: 返回领取成功
            App->>MQ: 发送异步消息(模板ID, 操作量-1)
            Note over MQ, DB: 异步同步阶段
            MQ->>DB: 消费消息，更新模板表库存: SET stock=stock-1
        else 事务执行失败 (领券失败)
            App->>DB: 回滚事务
            App->>Redis: INCR 库存Key (回滚库存)
            Note right of Redis: 回滚预扣库存
            alt 回滚成功
                App-->>Client: 返回领取失败（系统繁忙）
            else 回滚失败
                loop 最多重试3次
                    App->>Redis: INCR 库存Key (重试回滚)
                    alt 重试成功
                        Note right of App: 退出循环
                    else 重试失败
                        Note right of App: 继续重试
                    end
                end
                alt 最终回滚失败
                    App->>App: 监控上报，记录日志，人工介入
                    App-->>Client: 返回领取失败（系统繁忙）
                else 最终回滚成功
                    App-->>Client: 返回领取失败（系统繁忙）
                end
            end
        end
    else 预扣失败
        alt 剩余库存 n < 0
            App->>Redis: INCR 库存Key (校正为0)
            App-->>Client: 返回"已抢光"
        else 其他预扣失败原因
            App-->>Client: 返回"领取失败"
        end
    end
```

```mermaid
flowchart TD
    subgraph A[领券环节防护]
        A1[领取请求] --> A2{风控校验?}
        A2 -- 是 --> A3[执行风控规则]
        A3 --> A4{是否允许?}
        A4 -- 是 --> A5[允许领取]
        A4 -- 否 --> A6[拦截领取]
        
        A3_sub1[规则引擎] -.-> A3
        A3_sub2[实时计算] -.-> A3
    end

    subgraph B[用券环节防护]
        B1[核销请求] --> B2{风控校验?}
        B2 -- 是 --> B3[执行风控规则]
        B3 --> B4{是否允许?}
        B4 -- 是 --> B5[允许核销]
        B4 -- 否 --> B6[拦截核销]
        
        B3_sub1[规则引擎] -.-> B3
        B3_sub2[实时计算] -.-> B3
    end

    subgraph C[风控核心能力]
        C1[规则引擎]
        C2[实时画像]
        C3[名单管理<br>（黑/白/灰名单）]
        C4[机器学习模型]
    end

    subgraph D[治理与运营]
        D1[监控大盘]
        D2[实时告警]
        D3[人工审核与处置]
    end

    C --> A
    C --> B
    A & B --> D
```

```mermaid
sequenceDiagram
    participant User as 用户
    participant Client as 客户端(APP/WEB)
    participant Order as 订单系统
    participant Payment as 支付系统
    participant Channel as 第三方支付渠道<br>(如支付宝)
    participant MQ as 消息队列(MQ)
    participant Callback as 支付系统<br>回调处理服务

    User->>Client: 1. 提交订单
    Client->>Order: 2. 创建订单请求
    Order->>Order: 3. 生成订单号，状态:待支付
    Order-->>Client: 4. 返回订单创建成功
    Client-->>User: 5. 跳转到收银台

    User->>Client: 6. 选择支付方式，确认支付
    Client->>Order: 7. 发起支付请求(订单号)
    Order->>Payment: 8. 创建支付流水请求(订单信息)
    Payment->>Payment: 9. 生成支付流水号，状态:待支付
    Payment->>Payment: 10. 执行渠道路由
    Payment->>Channel: 11. 调用统一下单API
    Channel-->>Payment: 12. 返回支付参数(如二维码地址)
    Payment-->>Order: 13. 返回支付流水号及支付参数
    Order-->>Client: 14. 返回支付参数
    Client->>Client: 15. 渲染支付界面(如生成二维码)

    User->>Channel: 16. 在渠道页面完成支付<br>(输入密码/指纹)
    Channel->>Channel: 17. 渠道内部处理支付
    Channel-->>Callback: 18. 异步回调通知(POST)<br>告知支付最终结果
    Callback->>Callback: 19. 对回调进行验签
    Callback->>Payment: 20. 查询支付单状态(幂等校验)
    alt 支付成功且未处理过
        Payment->>Payment: 21. 更新支付流水状态为成功
        Payment->>MQ: 22. 发送支付成功消息
        MQ->>Order: 23. 消费消息，更新订单状态为已支付
        Order->>Order: 24. 触发后续履约逻辑(如减库存)
    end
    Callback-->>Channel: 25. 返回回调接收成功("SUCCESS")

    loop 支付状态查询(前端轮询)
        Client->>Order: 26. 查询订单支付状态
        alt 订单已支付
            Order-->>Client: 27. 返回支付成功
            Client-->>User: 28. 显示支付成功页面
        else 订单仍待支付
            Order-->>Client: 29. 返回待支付，继续轮询
        end
    end

```

```mermaid
sequenceDiagram
    participant Scheduler as 任务调度中心
    participant Reconciliation as 对账服务
    participant SFTP as 支付渠道SFTP
    participant DB as 支付系统数据库
    participant ChannelAPI as 支付渠道API
    participant Notification as 通知服务
    participant Admin as 人工处理台

    Note over Scheduler, Reconciliation: T+1日定时触发阶段
    Scheduler->>Reconciliation: 触发对账任务(T日数据)

    Note over Reconciliation, SFTP: 数据获取阶段
    Reconciliation->>SFTP: 1. 登录并下载渠道对账文件
    SFTP-->>Reconciliation: 2. 返回文件流
    Reconciliation->>DB: 3. 查询T日交易记录
    DB-->>Reconciliation: 4. 返回数据集

    Note over Reconciliation, Reconciliation: 核心对账阶段
    Reconciliation->>Reconciliation: 5. 解析&标准化数据
    loop 逐笔比对
        Reconciliation->>Reconciliation: 6. 以渠道记录为基准比对
    end

    alt 案例: 平账
        Reconciliation->>DB: 7a. 更新交易状态为“已对账”
    else 案例: 长款 (内部有, 渠道无)
        Reconciliation->>DB: 7b. 标记状态为“长款”
        Reconciliation->>ChannelAPI: 7c. 查询订单最终状态
        ChannelAPI-->>Reconciliation: 7d. 返回状态(如:失败)
        Reconciliation->>DB: 7e. 更新内部状态为“支付失败”
    else 案例: 短款 (渠道有, 内部无)
        Reconciliation->>DB: 7f. 执行“补单”操作<br>创建记录,状态更新为“成功”
    else 案例: 金额不一致
        Reconciliation->>DB: 7g. 标记为“差异”并冻结资金
        Reconciliation->>DB: 7h. 生成人工处理单
    end

    Note over Reconciliation, Notification: 报告与通知阶段
    Reconciliation->>DB: 8. 存储对账报告
    Reconciliation->>Notification: 9. 发送对账完成通知
    Notification->>Admin: 10. 邮件/钉钉通知(含报告链接)

    opt 人工介入(仅在出现差异时)
        Admin->>Admin: 11. 登录处理台
        Admin->>DB: 12. 查询差异订单详情
        Admin->>DB: 13. 进行人工调账操作
    end

    Reconciliation-->>Scheduler: 14. 返回任务完成
```

```mermaid
sequenceDiagram
    participant User as 用户
    participant Client as 商户客户端<br>(APP/WEB)
    participant Order as 商户订单系统
    participant Payment as 商户支付系统
    participant WeChat as 微信支付平台

    User->>Client: 1. 提交订单
    Client->>Order: 2. 创建订单请求
    Order->>Order: 3. 生成订单号，状态:待支付
    Order-->>Client: 4. 返回订单信息
    Client-->>User: 5. 展示订单，等待支付

    User->>Client: 6. 点击“支付”按钮
    Client->>Payment: 7. 发起支付请求(订单号)
    Payment->>Payment: 8. 生成支付流水号，状态:待支付
    Payment->>WeChat: 9. 调用统一下单API
    WeChat-->>Payment: 10. 返回预支付交易会话标识<br>(prepay_id)
    Payment-->>Client: 11. 返回支付所需参数<br>(含prepay_id)
    Client->>WeChat: 12. 调起微信支付控件
    WeChat-->>User: 13. 展示支付弹窗(输入密码/指纹)

    User->>WeChat: 14. 授权并确认支付
    WeChat->>WeChat: 15. 处理支付，扣减用户余额
    WeChat-->>Payment: 16. 异步回调通知(Callback)<br>告知支付最终结果
    Payment->>Payment: 17. 验签、查询支付单状态
    Payment->>Payment: 18. 更新支付流水状态为“成功”
    Payment->>Order: 19. 通知支付成功
    Order->>Order: 20. 更新订单状态为“已支付”
    Order-->>Client: 21. 推送支付成功消息
    Client-->>User: 22. 显示支付成功页面

    loop 前端轮询(可选)
        Client->>Order: 23. 查询订单支付状态
        Order-->>Client: 24. 返回状态
    end
```

```mermaid
sequenceDiagram
    participant User as 用户
    participant Client as 商户客户端
    participant MW as 微信支付接入网关
    participant Order as 交易订单服务
    participant Route as 支付路由服务
    participant Channel as 支付渠道服务
    participant Acc as 账户服务
    participant Ledger as 记账服务
    participant Risk as 风控服务
    participant Cache as 缓存
    participant DB as 核心数据库
    participant Bank as 银行/清算机构

    User->>Client: 1. 确认支付（输入密码/指纹）
    Client->>MW: 2. 发送支付授权请求（含prepay_id）
    MW->>Order: 3. 查询订单信息
    Order->>Cache: 4. 读取订单数据
    Cache-->>Order: 5. 返回订单详情
    Order-->>MW: 6. 返回订单信息

    MW->>Risk: 7. 实时风控校验
    Risk-->>MW: 8. 返回风控结果

    alt 风控拒绝
        MW-->>Client: 支付失败（风控拦截）
    else 风控通过
        MW->>Route: 9. 请求路由决策
        Route-->>MW: 10. 返回最优支付渠道<br>（如：工行借记卡快捷）
        MW->>Channel: 11. 请求渠道执行支付
        Channel->>Bank: 12. 发送支付指令<br>（扣款请求）
        Bank-->>Channel: 13. 返回扣款结果
        Channel-->>MW: 14. 返回渠道处理结果

        alt 渠道扣款失败
            MW-->>Client: 支付失败（余额不足等）
        else 渠道扣款成功
            MW->>Acc: 15. 请求更新账户状态
            Acc->>DB: 16. 扣减用户余额
            Acc-->>MW: 17. 确认更新成功
            MW->>Ledger: 18. 发起记账请求<br>（借贷记账）
            Ledger->>DB: 19. 写入记账流水
            Ledger-->>MW: 20. 确认记账完成
            MW->>Order: 21. 请求更新订单状态
            Order->>DB: 22. 更新订单状态为“支付成功”
            Order-->>MW: 23. 确认更新成功
            MW-->>Client: 24. 返回支付成功
            Client-->>User: 25. 展示支付成功
        end
    end
```

```mermaid
sequenceDiagram
    participant User as 中国用户
    participant MerchantApp as 境外商户APP
    participant MerchantServer as 境外商户服务器
    participant WCPayGateway as 微信支付<br>跨境接入网关
    participant WCPayCore as 微信支付<br>核心系统
    participant FXService as 汇率服务
    participant UserBank as 用户发卡行
    participant CardNetwork as 国际卡组织<br>(Visa/Mastercard)
    participant MerchantAcquirer as 商户收单行

    rect rgb(250, 250, 230)
        Note over User, WCPayGateway: 阶段一：支付发起与前置处理
        User->>MerchantApp: 1. 提交订单，选择WeChat Pay
        MerchantApp->>MerchantServer: 2. 创建订单(金额:100 USD)
        MerchantServer->>WCPayGateway: 3. 发起支付请求<br>(传递订单号、100 USD等信息)
        WCPayGateway->>FXService: 4. 查询实时汇率(USD/CNY)
        FXService-->>WCPayGateway: 5. 返回汇率(eg. 1:7.2)
        WCPayGateway->>WCPayGateway: 6. 计算人民币金额(720 CNY)
        WCPayGateway-->>MerchantServer: 7. 返回支付参数(含支付二维码信息)
        MerchantServer-->>MerchantApp: 8. 返回支付参数
        MerchantApp-->>User: 9. 展示支付二维码(金额:720 CNY)
    end

    rect rgb(230, 250, 230)
        Note over User, WCPayCore: 阶段二：国内支付与人民币扣款
        User->>User: 10. 扫描二维码，打开微信
        User->>WCPayCore: 11. 确认支付并授权(720 CNY)
        WCPayCore->>UserBank: 12. 执行人民币扣款授权(720 CNY)
        UserBank-->>WCPayCore: 13. 扣款成功
        WCPayCore-->>User: 14. 提示支付成功
        WCPayCore->>MerchantServer: 15. (异步)通知支付成功
        MerchantServer->>MerchantServer: 16. 更新订单状态为“已支付”
        MerchantServer-->>MerchantApp: 17. 更新前端状态
        MerchantApp-->>User: 18. 展示商户确认页面
    end

    rect rgb(230, 230, 250)
        Note over WCPayCore, MerchantAcquirer: 阶段三：跨境清结算(T+1日)
        WCPayCore->>WCPayCore: 19. 日终批处理<br>聚合所有跨境交易
        WCPayCore->>FXService: 20. 批量换汇(人民币->美元)
        WCPayCore->>CardNetwork: 21. 提交清算文件<br>(应结算给商户的美元金额)
        CardNetwork->>MerchantAcquirer: 22. 执行清算<br>划拨美元资金
        MerchantAcquirer->>MerchantAcquirer: 23. 贷记商户账户(100 USD)
        MerchantAcquirer-->>CardNetwork: 24. 清算完成确认
        CardNetwork-->>WCPayCore: 25. 清算结果回调
        WCPayCore->>WCPayCore: 26. 更新结算状态为“成功”
    end
```

```mermaid
sequenceDiagram
    participant User as 用户
    participant Merchant as 境外商户
    participant PaymentGateway as 支付机构<br>跨境网关
    participant FXCache as 汇率缓存<br>(Redis/Memcached)
    participant FXProvider as 外部汇率服务商<br>(如路孚特、彭博)
    participant RiskEngine as 风控引擎
    participant Accounting as 账务系统

    Note over User, PaymentGateway: 阶段一：支付发起与汇率查询
    User->>Merchant: 1. 提交订单(金额:100 USD)
    Merchant->>PaymentGateway: 2. 发起支付请求(订单号, 100 USD)
    
    PaymentGateway->>FXCache: 3. 查询缓存汇率(USD/CNY)
    alt 缓存中有有效汇率
        FXCache-->>PaymentGateway: 4a. 返回缓存的汇率(eg. 7.20)
    else 缓存无效/过期
        PaymentGateway->>FXProvider: 4b. 请求实时汇率(USD/CNY)
        FXProvider-->>PaymentGateway: 4c. 返回实时汇率(eg. 7.2005)
        PaymentGateway->>FXCache: 4d. 更新缓存(设置TTL)
    end

    Note over PaymentGateway, Accounting: 阶段二：内部加价与风控
    PaymentGateway->>PaymentGateway: 5. 计算最终报价汇率<br>（实时汇率 + 业务点差 = 7.25）
    PaymentGateway->>RiskEngine: 6. 风控校验(频率、金额)
    RiskEngine-->>PaymentGateway: 7. 风控通过

    Note over PaymentGateway, User: 阶段三：价格展示与用户确认
    PaymentGateway->>PaymentGateway: 8. 计算人民币金额<br>100 USD * 7.25 = 725 CNY
    PaymentGateway-->>Merchant: 9. 返回支付参数(含725 CNY)
    Merchant-->>User: 10. 展示最终支付金额(725 CNY)

    User->>User: 11. 确认支付
    User->>PaymentGateway: 12. 发起支付授权(725 CNY)

    Note over PaymentGateway, Accounting: 阶段四：支付完成与成本锁定
    PaymentGateway->>Accounting: 13. 记录交易与锁定汇率(7.25)
    Accounting-->>PaymentGateway: 14. 记录成功
```

```mermaid
flowchart TD
    A[用户发起跨境支付请求]
    B[支付机构系统]

    subgraph S1 [第一步：获取基准]
        C[查询外部汇率服务商<br>或内部缓存]
        D[获取实时基准汇率<br>如: 1 USD = 7.2000 CNY]
    end

    subgraph S2 [第二步：计算点差]
        E[量化引擎启动]
        F[计算对冲成本<br>评估风险溢价<br>加入运营利润]
        G[输出动态点差<br>如: 0.0500]
    end

    subgraph S3 [第三步：合成报价]
        H[合成最终报价汇率<br>7.2000 + 0.0500 = 7.2500]
        I[计算最终人民币金额<br>100 USD * 7.25 = 725 CNY]
    end

    A --> B
    B --> S1
    S1 --> S2
    S2 --> S3
    S3 --> J[向用户展示最终价格: 725 CNY]

    style S2 fill:#e6f7ff
```

