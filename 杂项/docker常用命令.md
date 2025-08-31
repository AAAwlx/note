好的，我来整理一下 **Docker 常用命令**，按照类别分清楚，方便日常使用和记忆。

---

## 1️⃣ 镜像相关命令

| 命令                                  | 功能                | 示例                                   |
| ----------------------------------- | ----------------- | ------------------------------------ |
| `docker pull <image>`               | 拉取镜像              | `docker pull nginx:latest`           |
| `docker images`                     | 查看本地镜像            | `docker images`                      |
| `docker rmi <image>`                | 删除镜像              | `docker rmi nginx:latest`            |
| `docker tag <image> <new_image>`    | 镜像重命名/打标签         | `docker tag nginx:latest mynginx:v1` |
| `docker build -t <name:tag> <path>` | 从 Dockerfile 构建镜像 | `docker build -t myapp:1.0 ./`       |

---

## 2️⃣ 容器相关命令

| 命令                                  | 功能           | 示例                                           |
| ----------------------------------- | ------------ | -------------------------------------------- |
| `docker run [options] <image>`      | 创建并运行容器      | `docker run -it --name mynginx nginx:latest` |
| `docker ps`                         | 查看运行中的容器     | `docker ps`                                  |
| `docker ps -a`                      | 查看所有容器（包括停止） | `docker ps -a`                               |
| `docker stop <container>`           | 停止容器         | `docker stop mynginx`                        |
| `docker start <container>`          | 启动已停止容器      | `docker start mynginx`                       |
| `docker restart <container>`        | 重启容器         | `docker restart mynginx`                     |
| `docker rm <container>`             | 删除容器         | `docker rm mynginx`                          |
| `docker exec -it <container> <cmd>` | 在容器内执行命令     | `docker exec -it mynginx bash`               |
| `docker logs <container>`           | 查看容器日志       | `docker logs mynginx`                        |

---

## 3️⃣ 网络与端口

| 命令                                 | 功能      | 示例                              |
| ---------------------------------- | ------- | ------------------------------- |
| `docker network ls`                | 查看网络    | `docker network ls`             |
| `docker network inspect <name>`    | 查看网络详情  | `docker network inspect bridge` |
| `docker run -p <host>:<container>` | 端口映射    | `docker run -p 8080:80 nginx`   |
| `docker network create <name>`     | 创建自定义网络 | `docker network create mynet`   |

---

## 4️⃣ 数据卷相关

| 命令                              | 功能  | 示例                                 |
| ------------------------------- | --- | ---------------------------------- |
| `docker volume ls`              | 查看卷 | `docker volume ls`                 |
| `docker volume create <name>`   | 创建卷 | `docker volume create mydata`      |
| `docker volume rm <name>`       | 删除卷 | `docker volume rm mydata`          |
| `docker run -v <volume>:<path>` | 挂载卷 | `docker run -v mydata:/data nginx` |

---

## 5️⃣ 系统管理命令

| 命令                    | 功能             | 示例                       |
| --------------------- | -------------- | ------------------------ |
| `docker info`         | 查看 Docker 系统信息 | `docker info`            |
| `docker system df`    | 查看磁盘使用情况       | `docker system df`       |
| `docker system prune` | 清理未使用的资源       | `docker system prune -a` |
| `docker version`      | 查看版本           | `docker version`         |

---

## 6️⃣ 镜像和容器调试

| 命令                                 | 功能         | 示例                       |
| ---------------------------------- | ---------- | ------------------------ |
| `docker inspect <container/image>` | 查看详细信息     | `docker inspect mynginx` |
| `docker stats`                     | 实时查看容器资源使用 | `docker stats`           |
| `docker top <container>`           | 查看容器内进程    | `docker top mynginx`     |
| `docker diff <container>`          | 查看容器文件系统改动 | `docker diff mynginx`    |
