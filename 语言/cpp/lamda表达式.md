# lamda 表达式

```cpp
[capture list] (params list) mutable exception-> return type { function body }
```

* capture list：捕获外部变量列表
* params list：形参列表
* mutable指示符：用来说用是否可以修改捕获的变量
* exception：异常设定
* return type：返回类型
* function body：函数体

对于lamad表达式来说，作用一般有捕获变量和作为一些函数的调用时简单的回调函数

在C++中，当你在一个lambda表达式中捕获局部变量时，你可以使用捕获列表（capture list）来实现。捕获列表位于lambda表达式的方括号`[]`内，可以包含一个或多个要捕获的变量。

捕获局部变量有两种方式：

### 1. 按值捕获

按值捕获意味着将局部变量的值复制到lambda表达式中。这样，即使原始变量的值在lambda表达式之外发生变化，lambda表达式内部使用的变量值也不会受到影响。

语法：

```cpp
[capture_list] (parameters) -> return_type { function_body }
```

示例：

```cpp
#include <iostream>

int main() {
    int x = 10;

    // 按值捕获x
    auto lambda = [x]() {
        std::cout << "The value of x inside the lambda is: "<< x << std::endl;
    };

    x = 20;
    lambda(); // 输出 "The value of x inside the lambda is: 10"
}
```

### 2. 按引用捕获

按引用捕获意味着将局部变量的引用传递给lambda表达式。这样，lambda表达式内部使用的变量值将与原始变量的值保持一致。

语法：

```cpp
[capture_list] (parameters) -> return_type { function_body }
```

示例：

```cpp
#include <iostream>

int main() {
    int x = 10;

    // 按引用捕获x
    auto lambda = [&x]() {
        std::cout << "The value of x inside the lambda is: "<< x << std::endl;
    };

    x = 20;
    lambda(); // 输出 "The value of x inside the lambda is: 20"
}
```

需要注意的是，当按引用捕获局部变量时，要确保在lambda表达式执行期间局部变量仍然有效。否则，如果局部变量在lambda表达式执行之前被销毁，那么lambda表达式内部将访问到一个悬空引用，导致未定义行为。

如果你想要同时按值捕获和按引用捕获变量，可以在捕获列表中同时列出它们：

```cpp
#include <iostream>

int main() {
    int x = 10;
    int y = 20;

    // 按值捕获x，按引用捕获y
    auto lambda = [x, &y]() {
        std::cout << "The value of x inside the lambda is: "<< x << std::endl;
        std::cout << "The value of y inside the lambda is: "<< y << std::endl;
    };

    x = 30;
    y = 40;
    lambda(); // 输出 "The value of x inside the lambda is: 10" 和 "The value of y inside the lambda is: 40"
}
```

在这个示例中，我们按值捕获了`x`，按引用捕获了`y`。因此，lambda表达式内部使用的`x`值不受外部`x`值变化的影响，而`y`值则与外部`y`值保持一致。