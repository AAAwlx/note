# 运行一个go项目

## go项目的组织

go 中使用包来导入外部库，下面示例导入的包中既有官方提供的库，如：io 等，也有自己提供的库如 ./client ，引入该路径就可以使用下面的函数。

```go
import (
    "bytes"
    "fmt"
    "io"
    "log"
    "strconv"
    "strings"
    "./client"
)
```

生成 go.mod

```go
go mod init myproject//项目名
```

构建可执行程序

```go
go build -o myapp ./cmd/myapp
```