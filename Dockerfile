# Janus Dynamic Binary Translator - Development Image
#基于 Ubuntu 22.04，包含完整的构建和测试环境

FROM ubuntu:22.04

LABEL maintainer="Janus Project"
LABEL description="ARM to x86_64 Dynamic Binary Translation Library"
LABEL version="1.0.0"

# 避免交互式提示
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 安装构建工具
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    make \
    gcc \
    gdb \
    git \
    pkg-config \
    vim \
    && rm -rf /var/lib/apt/lists/*

# 安装测试工具
RUN apt-get update && apt-get install -y \
    qemu-user \
    qemu-system-arm \
    valgrind \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /workspace

# 复制项目文件
COPY . /workspace/

# 构建项目
RUN mkdir -p build && cd build && cmake .. -DJANUS_BUILD_TESTS=ON -DJANUS_BUILD_TOOLS=ON && make -j$(nproc)

# 运行测试
RUN cd build && ctest --output-on-failure

# 设置环境变量
ENV JANUS_LIB_PATH=/workspace/build
ENV LD_LIBRARY_PATH=/workspace/build:$LD_LIBRARY_PATH

# 默认命令
CMD ["/bin/bash"]
