# gin框架

## Gin路由与控制器

一条路由规则由三部分组成：http请求方法、url路径、控制器函数

常用的http请求方法有下面4种:

1. GET：用于获取服务器上的资源，通常用来请求数据。比如：GET /users 用来获取用户列表。
2. POST：用于向服务器提交数据，通常用于创建新的资源。比如：POST /users 用来创建一个新的用户。
3. DELETE：用于删除服务器上的资源。比如：DELETE /users/1 用来删除 ID 为 1 的用户。
4. PATCH：用于部分更新服务器上的资源。比如：PATCH /users/1 用来更新 ID 为 1 的用户的一部分数据。
5. PUT：用于替换服务器上的资源，通常用于更新整个资源。比如：PUT /users/1 用来替换 ID 为 1 的用户的所有数据。
6. OPTIONS：用于获取服务器允许的请求方法，一般用于跨域请求前检查服务器允许的方法。
7. HEAD：类似于 GET 请求，但只请求资源的响应头，通常用于检查资源是否存在或获取元数据信息。

## 中间件

Gin框架需要使用中间件主要有以下几个原因：

**一、处理通用逻辑**
1. **日志记录**
   - 在每个请求处理过程中，记录相关信息（如请求的URL、方法、时间等）对于监控和调试非常关键。中间件可以在请求到达具体处理函数之前或之后统一进行日志记录操作，无需在每个路由处理函数中重复编写日志代码。
   - 例如，可以记录每个请求的处理时长，以便发现性能瓶颈。
2. **身份验证**
   - 对于许多Web应用，验证用户的身份是必不可少的。中间件可以在请求处理流程的前端检查用户的身份信息（如通过解析JWT令牌），如果验证失败，可以直接返回错误响应，阻止请求进一步到达需要授权的资源。
   - 这样就不需要在每个需要保护的路由处理函数中单独编写身份验证逻辑。

**二、增强框架功能**
1. **跨域资源共享（CORS）处理**
   - 当Web应用需要与不同源（域名、端口等）的资源进行交互时，需要正确处理CORS。中间件能够统一设置响应头中的相关CORS字段（如Access - Control - Allow - Origin等），使得整个Gin应用能够方便地支持跨域请求。
2. **压缩响应**
   - 为了减少网络传输的数据量，提高用户体验，可以对响应内容进行压缩。中间件可以在发送响应之前对数据进行压缩处理，而不需要在每个路由处理函数中考虑压缩逻辑。

**三、代码结构优化**
1. **分离关注点**
   - 它有助于将不同的功能模块分离开来。例如，路由定义主要关注URL和处理函数的映射关系，而中间件则专注于处理请求和响应过程中的通用任务。这样可以使代码更加清晰、易于维护和扩展。
2. **提高代码复用性**
   - 一旦编写了一个中间件来处理特定的任务（如错误处理中间件），就可以将其应用到多个路由或者整个Gin应用中，避免了在多个地方重复编写相同的代码片段。

```go
func main() {
  // Creates a router without any middleware by default
  r := gin.New()

  // Global middleware
  // Logger middleware will write the logs to gin.DefaultWriter even if you set with GIN_MODE=release.这里的Logger是集成在gin框架中的日志组件
  // By default gin.DefaultWriter = os.Stdout
  r.Use(gin.Logger())

  // Recovery middleware recovers from any panics and writes a 500 if there was one.
  r.Use(gin.Recovery())

  // Per route middleware, you can add as many as you desire.
  r.GET("/benchmark", MyBenchLogger(), benchEndpoint)

  // Authorization group
  // authorized := r.Group("/", AuthRequired())
  // exactly the same as:
  authorized := r.Group("/")
  // per group middleware! in this case we use the custom created
  // AuthRequired() middleware just in the "authorized" group.
  authorized.Use(AuthRequired())
  {
    authorized.POST("/login", loginEndpoint)
    authorized.POST("/submit", submitEndpoint)
    authorized.POST("/read", readEndpoint)

    // nested group
    testing := authorized.Group("testing")
    // visit 0.0.0.0:8080/testing/analytics
    testing.GET("/analytics", analyticsEndpoint)
  }

  // Listen and serve on 0.0.0.0:8080
  r.Run(":8080")
}
```
在上面的代码中使用 r.Use 来为所有路由都设置路由的中间件

如果要为单独的路由规则添加路由中间件，应该使用

```go
group.POST("/aaa", header.Headers(), work) // header.Headers()为中间件的逻辑实现，work为路由规则对应的处理函数 

func Headers() gin.HandlerFunc {
    return func(c *gin.Context) {
        // ...... 逻辑实现 
    }
}
```

[go中间件使用详解](https://blog.csdn.net/weixin_46618592/article/details/125588199)



