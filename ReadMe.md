# SDL3 GPU 例子

SDL3 GPU API相关的例子

所有的例子都在`examples`下。

## 要求的工具

* [glslc](https://github.com/google/shaderc)：用于编译glsl着色器。可从源码编译或者自行安装。

如果不想安装glslc到环境变量PATH下，也可以在cmake-gui中指定GLSLC路径（变量名`GLSLC_PROG`），如：

```bash
cmake -S . -B cmake-build -DGLSLC_PROG="C:/VulkanSDK/1.3.296.0/Bin/glslc.exe"
```

## 编译

首先使用git submodule拉取SDL3源码：

```bash
git submodule update --init --recursive --depth=1
```

然后使用cmake进行编译：

```bash
cmake --preset=default
cmake --build cmake-build
```

**所有程序均在根目录（就是本文件所在目录）下运行，否则会找不到渲染资产！**