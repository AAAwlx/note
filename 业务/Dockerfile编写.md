如果不使用 Go 的官方镜像，完全基于一个基础镜像（如 `ubuntu` 或 `alpine`）进行构建，你需要手动安装 Go 工具链。这种方法增加了复杂性，但可以更好地定制环境。以下是一个使用 `ubuntu` 基础镜像的示例 Dockerfile：

---

### **使用 Ubuntu 构建的 Dockerfile**
```dockerfile
# 使用 Ubuntu 作为基础镜像
FROM ubuntu:20.04

# 设置环境变量，避免交互式安装中的提示
ENV DEBIAN_FRONTEND=noninteractive

# 更新系统并安装必要的软件包
RUN apt-get update && apt-get install -y \
    curl \
    gcc \
    make \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 安装 Go（示例中使用 1.20.7）
RUN curl -OL https://golang.org/dl/go1.20.7.linux-amd64.tar.gz && \
    tar -C /usr/local -xzf go1.20.7.linux-amd64.tar.gz && \
    rm go1.20.7.linux-amd64.tar.gz

# 设置 Go 的环境变量
ENV PATH="/usr/local/go/bin:$PATH"

# 设置工作目录
WORKDIR /app

# 复制项目文件到容器中
COPY . .

# 构建 Go 程序
RUN go build -o main .

# 暴露应用端口（根据实际需求修改）
EXPOSE 8080

# 启动程序
CMD ["./main"]
```

---

### **说明**
1. **基础镜像选择**
   - 使用 `ubuntu:20.04`，因为它相对轻量，并且可以使用 `apt` 轻松安装工具。
   - 如果更注重体积，可以改用 `debian:bullseye-slim` 或 `alpine`，但需要额外配置。

2. **安装 Go**
   - 直接从 [Go 官方下载页面](https://golang.org/dl/)获取预编译的二进制文件。
   - 解压到 `/usr/local` 并添加到 `PATH` 环境变量中。

3. **构建程序**
   - 使用 `go build` 编译可执行文件。
   - 如果你的程序依赖特定环境变量或工具，可以在 `RUN` 中配置。

---

### **镜像构建与运行**

#### 构建镜像
```bash
docker build -t my-go-app .
```

#### 运行容器
```bash
docker run --rm -p 8080:8080 my-go-app
```

---

如果需要基于其他更小的基础镜像（如 `alpine`），需要安装 `bash`、`gcc` 等工具，我可以提供进一步的优化方案！

dockerfile指定了在制作docker镜像时要将哪些环境放进去。