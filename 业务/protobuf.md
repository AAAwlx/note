# protobuf

## 编写规范

以下是编写 Protocol Buffers（protobuf）时的规范和关键字的意义：

---

### **Protocol Buffers 编写规范**

1. **文件命名规范**
   - 使用 `.proto` 后缀命名文件。
   - 文件名应尽量清晰表达其用途，例如 `user.proto`、`order.proto`。

2. **包名（package）**
   - 每个 `.proto` 文件通常应定义一个 `package`，用于防止命名冲突。
   - 包名推荐使用小写字母，并按照项目的组织结构命名（类似 Java 包的命名方式），如：

     ```proto
     package com.example.user;
     ```

3. **字段命名**
   - 字段名使用 snake_case（下划线命名法）。
   - 确保字段名清晰且具有实际意义，如 `user_id`、`order_status`。

4. **字段编号**
   - 字段编号使用正整数（1-2^29-1），编号不能重复。
   - 1-15 号字段优先分配给经常使用的字段，因为它们的编码效率更高。
   - 若需要删除字段，应避免重复使用相同的编号，可以标记为 `reserved`：

     ```proto
     reserved 3, 5, 7; // 保留这些字段编号
     ```

5. **注释**
   - 使用双斜杠 `//` 添加注释，描述字段或消息的用途。

6. **版本控制**
   - 使用 `syntax = "proto3";` 指定版本（推荐使用 `proto3`）。

7. **保持向后兼容**
   - 不要随意删除或重命名字段。
   - 添加新字段时，使用未使用的字段编号。

---

### **Protocol Buffers 关键字的意义**

#### **1. `syntax`**

- 用于指定 protobuf 的版本。
- 语法：

  ```proto
  syntax = "proto3";
  ```

#### **2. `package`**

- 定义消息所属的命名空间，防止命名冲突。
- 语法：

  ```proto
  package mypackage;
  ```

#### **3. `message`**

- 定义一个数据结构（类似于类或结构体）。
- 语法：
  
  ```proto
  message User {
      int32 id = 1;         // 用户 ID
      string name = 2;      // 用户名
      bool is_active = 3;   // 是否活跃
  }
  ```

#### **4. 字段类型**

- 用于定义字段的数据类型，包括以下常见类型：
  - 基础类型：
    - `int32`, `int64`: 有符号整数。
    - `uint32`, `uint64`: 无符号整数。
    - `float`, `double`: 浮点数。
    - `bool`: 布尔值。
    - `string`: 字符串。
    - `bytes`: 二进制数据。
  - 嵌套类型：
    - 可以在消息中嵌套其他消息类型。
  - 枚举类型：
    - 定义一组固定的常量值。

#### **5. `enum`**

- 定义枚举类型，用于表示有限的选项。
- 语法：
  
  ```proto
  enum Status {
      UNKNOWN = 0;
      ACTIVE = 1;
      INACTIVE = 2;
  }
  ```

#### **6. `repeated`**

- 定义一个重复字段（类似数组或列表）。
- 语法：
  
  ```proto
  message User {
      repeated string tags = 1; // 标签列表
  }
  ```

#### **7. `optional` 和 `required`**

- **Proto3:** 不再区分 `optional` 和 `required`，所有字段默认可选。
- **Proto2:** 可通过 `optional` 和 `required` 指定字段是否必须。
  
  ```proto
  optional string nickname = 4; // 可选字段
  required int32 age = 5;       // 必须字段
  ```

#### **8. `reserved`**

- 保留字段编号或名称，防止未来使用。
- 语法：

  ```proto
  reserved 3, 5;
  reserved "old_name";
  ```

#### **9. `import`**

- 导入其他 `.proto` 文件中的定义。
- 语法：

  ```proto
  import "common.proto";
  ```

#### **10. 服务关键字（`service`、`rpc`）**

- 用于定义 RPC 服务。
- 语法：
  
  ```proto
  service UserService {
      rpc GetUser(UserRequest) returns (UserResponse);
  }
  ```

---

### **完整示例**

以下是一个包含上述关键字的示例 `.proto` 文件：

```proto
syntax = "proto3";

package com.example.user;

message User {
    int32 id = 1;              // 用户 ID
    string name = 2;           // 用户名
    bool is_active = 3;        // 是否活跃
    repeated string tags = 4;  // 用户标签
}

enum Status {
    UNKNOWN = 0;
    ACTIVE = 1;
    INACTIVE = 2;
}

message UserRequest {
    int32 id = 1;              // 请求的用户 ID
}

message UserResponse {
    User user = 1;             // 用户信息
}

service UserService {
    rpc GetUser(UserRequest) returns (UserResponse); // 获取用户信息
}
```

通过遵循这些规范和正确使用关键字，可以确保 protobuf 文件清晰易懂并具有良好的可维护性。

## protobuf生成cpp代码

通过.proto文件可以生成cpp代码

生成指令：

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

### 字段值操作方法

#### 单个数值字段

```cpp
int32 foo = 1;
```

int32 foo() const：返回字段目前的值。如果字段未设置，返回0。
void set_foo(int32 value)：设置字段的值。调用之后，foo()会返回value。
void clear_foo()：清空字段的值。调用之后，foo()将返回0。

#### 字符串

```cpp
string foo = 1;
bytes foo = 1;
```

对于字段中包含字段的情况包含以下方法：

* const string& foo() const：返回字段当前的值。如果字段未设置，则返回空string/bytes。
* void set_foo(const string& value)：设置字段的值。调用之后，foo()将返回value的拷贝。
* void set_foo(string&& value)（C++11及之后）：设置字段的值，从传入的值中移入。调用之后，foo()将返回value的拷贝。
* void set_foo(const char* value)：使用C格式的空终止字符串设置字段的值。调用之后，foo()将返回value的拷贝。
* void set_foo(const char* value, int size)：如上，但使用的给定的大小而不是寻找空终止符。
* string* mutable_foo()：返回存储字段值的可变string对象的指针。若在字段设置之前调用，则返回空字符串。调用之后，foo()会将写入值返回给给定的字符串。
* void clear_foo()：：清空字段的值。调用之后，foo()将返回空string/bytes。
* void set_allocated_oo(string* value)：设置字段为给定string对象，若已存在，则释放之前的字段值。如果string指针非NULL，消息将获取已分配的string对象的所有权。消息可以在任何时候删除已分配的string对象，因此对该对象的引用可能无效。另外，若value为NULL，该操作与调用clear_foo()效果相同。
* string* release_foo()：释放字段的所有权并返回string对象的指针。调用之后，调用者将获得已分配的string对象的所有权，foo()将返回空string/bytes。

### rpc接口

对于rpc接口，proto有如下定义

```proto
// 定义服务和RPC方法
service UserService {
    // 获取用户信息
    rpc GetUser(GetUserRequest) returns (GetUserResponse);

    // 创建新用户
    rpc CreateUser(CreateUserRequest) returns (CreateUserResponse);
}
```

会生成如下内容：

```cpp
::grpc::Status UserService::Service::GetUser(::grpc::ServerContext* context, const ::example::GetUserRequest* request, ::example::GetUserResponse* response) {
  (void) context;
  (void) request;
  (void) response;
  int id = request->user_id();
  if(id!=0){
      response->mutable_user()->set_id(1);
      response->mutable_user()->set_email("aaa");
      response->mutable_user()->add_addresses();//只有使用 repeated 关键字的变量可以使用add字段，类似于一个容器
      response->mutable_user()->add_addresses();
      response->mutable_user()->mutable_addresses(0)->set_street(1);
      example::User *user = response->release_user();
  }
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "");
}

::grpc::Status UserService::Service::CreateUser(::grpc::ServerContext* context, const ::example::CreateUserRequest* request, ::example::CreateUserResponse* response) {
  (void) context;
  (void) request;
  (void) response;
  std::string name1 = request->name();
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "");
}
```

UserService 为类名
CreateUser 为函数名

## 在客户端调用rpc

```cpp

#include <grpcpp/grpcpp.h>
#include "user_service.grpc.pb.h"

int main() {
    // 1. 创建一个与服务端通信的通道
    auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());

    // 2. 创建 UserService 的 Stub 对象
    auto stub = example::UserService::NewStub(channel);

    // 3. 构造请求消息
    example::CreateUserRequest request;
    request.set_name("Alice");

    // 4. 构造响应消息
    example::CreateUserResponse response;

    // 5. 调用 RPC 方法
    grpc::ClientContext context;
    grpc::Status status = stub->CreateUser(&context, request, &response);

    // 6. 检查状态并处理响应
    if (status.ok()) {
        std::cout << "CreateUser success: " << response.status() << std::endl;
    } else {
        std::cerr << "RPC failed: " << status.error_message() << std::endl;
    }

    return 0;
}

```

[protobuf CPP 代码生成规律](https://www.cnblogs.com/lianshuiwuyi/p/12291208.html)

```shell
 protoc --go_out=. --go-grpc_out=. example.proto 
 protoc -I=. --grpc_out=. --plugin=protoc-gen-grpc=/usr/local/bin/grpc_cpp_plugin example.proto 
```