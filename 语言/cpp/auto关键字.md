# auto关键字

作用

让编译器自动推导变量的类型,对于一些不知道变量类型的场景来说可以使用 auto 关键字，比如当你调用别人的写的函数却找不到他函数的定义时。

使用示例

```
auto x = 10;
```


不适用场景

例如

```cpp
#include<iostream>
char* HHH()
{
	return "HHH";
}
int main()
{
	auto name=HHH();
    int a=name.size()
}
```

在示例代码中，类型的改变无法被人所知道，会引起后续于类型相关的一些操作的错误