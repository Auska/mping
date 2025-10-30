#!/bin/bash

# 构建脚本用于编译mping项目

# 检查是否提供了参数
if [ "$#" -eq 0 ]; then
    BUILD_TYPE="Release"
else
    BUILD_TYPE="$1"
fi

echo "Building mping in $BUILD_TYPE mode..."

# 创建build目录（如果不存在）
mkdir -p build
cd build

# 配置项目
if [ "$BUILD_TYPE" = "Debug" ]; then
    cmake -DCMAKE_BUILD_TYPE=Debug ..
else
    cmake -DCMAKE_BUILD_TYPE=Release ..
fi

# 编译项目
make -j$(nproc)

# 检查编译是否成功
if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Executable is located at: build/mping"
else
    echo "Build failed!"
    exit 1
fi