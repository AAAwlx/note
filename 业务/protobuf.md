# protobuf

## 编写规范



## protobuf生成cpp代码

通过.proto文件可以生成cpp代码

如果在proto文件中使用 package ，在该proto中的message会被放入包名的命名空间中。例如：

```proto
package packname //packname为包名

message message1{
    ...
}

message message2{
    ...
}
```

而在该package之下的 message 则会成为该命名空间下的一个类

```cpp
namespace packname
{
    class message1;
    class message2;
    ...
}
```

对于一个 message1 类，会包含以下内容

1. 构造方法，对于构造方法又有默认的构造方法和拷贝构造方法
2. 析构方法
3. Swap
4. 默认实例 internal_default_instance
   函数的作用是获取 AddBalanceReq 类型的默认实例的指针。通过使用 static inline 关键字，它被设计为一个内部使用的、高效的函数。reinterpret_cast 用于确保返回的指针类型正确，即使 _AddBalanceReq_default_instance_ 的实际类型可能与 AddBalanceReq 不同（但在本例中，它们应该是相同的）。
   这种写法通常在定义序列化/反序列化库或类似框架时使用，用于获取默认实例以便进行对象的初始化或比较。

除了上述的方法，对于message中的字段每个字段都有对应的操作方法

```proto
message{
    optional uint64 a = 1；
    optional string b = 2；
}
```

其中对于任何字段都有最基本的 

set clear has 方法

对于字段中包含字段的情况包含以下方法

通过以下方法能将