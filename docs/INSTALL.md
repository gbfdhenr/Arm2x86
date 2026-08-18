# Arm2x86 安装指南

本文档介绍如何在不同平台上安装 Arm2x86 动态二进制翻译库。

## 目录

1. [系统要求](#系统要求)
2. [Linux 平台安装](#linux 平台安装)
3. [macOS 安装](#macos 安装)
4. [Docker 安装](#docker 安装)
5. [从源码编译](#从源码编译)
6. [验证安装](#验证安装)
7. [故障排查](#故障排查)

---

## 系统要求

### 硬件要求

| 组件 | 最低要求 | 推荐配置 |
|------|---------|---------|
| CPU | x86_64 架构 | 支持 AVX2 的 Intel/AMD CPU |
| 内存 | 512 MB | 2 GB 以上 |
| 磁盘 | 100 MB | 500 MB 以上 |

### 软件要求

| 系统 | 版本 | 备注 |
|------|------|------|
| Linux | Kernel 4.4+ | Ubuntu 18.04+, Fedora 28+, CentOS 7+ |
| macOS | 10.13+ | 需要 Xcode Command Line Tools |
| Docker | 19.03+ | 可选容器化方案 |

### 编译工具

- **GCC** 7.0+ 或 **Clang** 6.0+
- **CMake** 3.10+ （可选，推荐使用 Make）
- **Make** 4.0+
- **pkg-config** （可选）

---

## Linux 平台安装

### Ubuntu/Debian

```bash
# 1. 安装依赖
sudo apt-get update
sudo apt-get install -y build-essential git cmake pkg-config

# 2. 克隆仓库
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86

# 3. 编译
make -j$(nproc)

# 4. 安装（可选）
sudo make install

# 5. 更新库缓存
sudo ldconfig
```

### Fedora/RHEL/CentOS

```bash
# 1. 安装依赖
sudo dnf groupinstall "Development Tools"
sudo dnf install git cmake pkg-config

# 2. 克隆仓库
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86

# 3. 编译
make -j$(nproc)

# 4. 安装（可选）
sudo make install

# 5. 更新库缓存
sudo ldconfig
```

### openSUSE

```bash
# 1. 安装依赖
sudo zypper install -y task-devel-base git cmake pkg-config

# 2. 编译安装
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86
make -j$(nproc)
sudo make install
sudo ldconfig
```

---

## macOS 安装

```bash
# 1. 安装 Xcode Command Line Tools
xcode-select --install

# 2. 使用 Homebrew 安装依赖
brew install git cmake pkg-config

# 3. 克隆仓库
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86

# 4. 编译（macOS 需要额外标志）
make CFLAGS="-I/usr/local/include" LDFLAGS="-L/usr/local/lib"

# 5. 安装
sudo make install
```

---

## Docker 安装

### 使用预构建镜像

```bash
# 拉取镜像
docker pull monkeycodeai/arm2x86:latest

# 运行容器
docker run -it --rm monkeycodeai/arm2x86:latest
```

### 自行构建镜像

```bash
# 构建镜像
docker build -t arm2x86:latest .

# 运行容器
docker run -it --rm -v $(pwd):/workspace arm2x86:latest
```

### Docker Compose

```yaml
version: '3'
services:
  arm2x86:
    build: .
    volumes:
      - ./:/workspace
    command: make test
```

---

## 从源码编译

### 标准编译

```bash
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86
make -j$(nproc)
```

### CMake 编译（推荐）

```bash
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86
mkdir build && cd build

# 配置
cmake .. \
  -DARM2X86_BUILD_TESTS=ON \
  -DARM2X86_BUILD_TOOLS=ON \
  -DARM2X86_ENABLE_NEON=ON \
  -DARM2X86_ENABLE_AVX=ON \
  -DCMAKE_INSTALL_PREFIX=/usr/local

# 编译
make -j$(nproc)

# 安装
sudo make install
```

### 自定义安装路径

```bash
# 安装到用户目录
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
make -j$(nproc)
make install

# 添加到 PATH 和 LD_LIBRARY_PATH
export PATH=$HOME/.local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/lib:$LD_LIBRARY_PATH
```

### 编译选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `ARM2X86_BUILD_TESTS` | 构建测试 | ON |
| `ARM2X86_BUILD_TOOLS` | 构建工具 | ON |
| `ARM2X86_ENABLE_NEON` | 启用 NEON 转译 | ON |
| `ARM2X86_ENABLE_AVX` | 启用 AVX 优化 | OFF |
| `ARM2X86_ENABLE_PERF` | 启用性能监控 | ON |

### 特殊编译目标

```bash
# 调试版本
make debug

# 性能监控版本
make perf

# AVX 优化版本
make avx

# 完整调试版本
make debug-all

# 清理
make clean
```

---

## 验证安装

### 检查库文件

```bash
# 检查库是否安装
ldconfig -p | grep arm2x86

# 应该看到类似输出：
# libarm2x86.so.1 (libc6,x86-64) => /usr/local/lib/libarm2x86.so.1
```

### 运行测试

```bash
# 进入项目目录
cd arm2x86

# 运行测试套件
make test
make run-test
```

### 简单测试程序

创建测试文件 `test_arm2x86.c`：

```c
#include <stdio.h>
#include "arm2x86_easy.h"

int main() {
    printf("Arm2x86 Version: %s\n", arm2x86_version_string());
    
    // 创建实例
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(NULL);
    if (!arm2x86) {
        fprintf(stderr, "Failed to create Arm2x86 instance\n");
        return 1;
    }
    
    printf("Arm2x86 instance created successfully\n");
    
    // 清理
    arm2x86_destroy_easy(arm2x86);
    printf("Arm2x86 instance destroyed\n");
    
    return 0;
}
```

编译并运行：

```bash
gcc -o test_arm2x86 test_arm2x86.c -larm2x86
./test_arm2x86
```

---

## 故障排查

### 问题 1: 编译时找不到头文件

**错误信息:**
```
fatal error: arm2x86.h: No such file or directory
```

**解决方案:**
```bash
# 确保当前目录正确
cd /path/to/arm2x86

# 或指定包含路径
gcc -I/path/to/arm2x86 -o test test.c
```

### 问题 2: 运行时找不到库

**错误信息:**
```
error while loading shared libraries: libarm2x86.so.1: cannot open shared object file
```

**解决方案:**
```bash
# 方法 1: 更新库缓存
sudo ldconfig

# 方法 2: 设置 LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 方法 3: 静态链接（不推荐）
gcc -static -o test test.c -larm2x86
```

### 问题 3: CMake 配置失败

**错误信息:**
```
CMake Error: Could not find CMakeLists.txt
```

**解决方案:**
```bash
# 确保在正确的目录运行 cmake
cd /path/to/arm2x86
mkdir build && cd build
cmake ..
```

### 问题 4: Docker 构建失败

**错误信息:**
```
RUN apt-get update failed
```

**解决方案:**
```bash
# 检查网络连接
docker run --rm ubuntu:22.04 apt-get update

# 使用国内镜像源
# 修改 Dockerfile 中的 apt-get 源
```

### 问题 5: 权限错误

**错误信息:**
```
Permission denied: make install
```

**解决方案:**
```bash
# 使用 sudo
sudo make install

# 或安装到用户目录
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
make install
```

---

## 卸载

### Linux

```bash
# 如果使用 make install 安装
cd /path/to/arm2x86
sudo make uninstall

# 或手动删除
sudo rm -rf /usr/local/lib/libarm2x86*
sudo rm -rf /usr/local/include/arm2x86*
sudo rm -rf /usr/local/share/pkgconfig/arm2x86.pc

# 更新库缓存
sudo ldconfig
```

### macOS

```bash
# 删除库文件
sudo rm -rf /usr/local/lib/libarm2x86*
sudo rm -rf /usr/local/include/arm2x86*
```

### Docker

```bash
# 删除镜像
docker rmi arm2x86:latest

# 删除容器
docker rm -f arm2x86_container
```

---

## 获取帮助

如遇到其他问题，请通过以下方式获取帮助：

1. 查阅 [FAQ.md](FAQ.md)
2. 查看 [USAGE.md](USAGE.md)
3. 提交 GitHub Issue
4. 查阅项目邮件列表

---

## 下一步

安装完成后，请查阅：

- [USAGE.md](USAGE.md) - 使用指南
- [API.md](API.md) - API 参考
- [FAQ.md](FAQ.md) - 常见问题
