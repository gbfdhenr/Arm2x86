# Arm2x86 文档导航

欢迎使用 Arm2x86 动态二进制翻译库！本文档索引帮助您快速找到所需信息。

## 📚 文档分类

### 入门指南

| 文档 | 描述 | 适合人群 |
|------|------|---------|
| [README.md](../README.md) | 项目概述和快速开始 | 所有用户 |
| [README_zh.md](../README_zh.md) | 中文版项目概述 | 中文用户 |
| [INSTALL.md](INSTALL.md) | 详细安装指南 | 新用户 |
| [FAQ.md](FAQ.md) | 常见问题解答 | 所有用户 |

### 使用指南

| 文档 | 描述 | 适合人群 |
|------|------|---------|
| [USAGE.md](USAGE.md) | 详细使用指南 | 开发者 |
| [API.md](API.md) | API 参考手册 | 开发者 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 架构设计文档 | 高级开发者 |

### 优化与调试

| 文档 | 描述 | 适合人群 |
|------|------|---------|
| [PERFORMANCE.md](PERFORMANCE.md) | 性能优化指南 | 性能工程师 |
| [FAQ.md](FAQ.md#故障排查) | 故障排查指南 | 所有用户 |
| [TESTING.md](TESTING.md) | 测试指南 | 测试人员 |

### 项目信息

| 文档 | 描述 | 适合人群 |
|------|------|---------|
| [CONTRIBUTING.md](CONTRIBUTING.md) | 贡献指南 | 贡献者 |
| [CHANGELOG.md](CHANGELOG.md) | 版本历史 | 所有用户 |
| [PROJECT_PLAN.md](PROJECT_PLAN.md) | 项目规划 | 项目成员 |

---

## 🚀 快速导航

### 我是新用户，想快速了解 Arm2x86

1. 阅读 [README.md](../README.md) 或 [README_zh.md](../README_zh.md)
2. 查看 [INSTALL.md](INSTALL.md) 安装指南
3. 参考 [FAQ.md](FAQ.md) 解决常见问题

### 我想开始使用 Arm2x86 开发

1. 阅读 [USAGE.md](USAGE.md) 了解基本用法
2. 查阅 [API.md](API.md) 了解 API 细节
3. 参考 [USAGE.md](USAGE.md) 中的代码示例

### 我想优化性能

1. 阅读 [PERFORMANCE.md](PERFORMANCE.md) 了解优化策略
2. 查看 [FAQ.md](FAQ.md#性能优化) 性能优化建议
3. 使用性能监控工具分析瓶颈

### 我遇到了问题

1. 查阅 [FAQ.md](FAQ.md#故障排查) 常见问题
2. 查看 [USAGE.md](USAGE.md) 确认使用方式
3. 在 GitHub 提交 Issue

### 我想贡献代码

1. 阅读 [CONTRIBUTING.md](CONTRIBUTING.md)
2. 查看 [TESTING.md](TESTING.md) 了解测试要求
3. Fork 仓库并提交 PR

---

## 📖 推荐阅读路径

### 路径 1: 快速上手 (30 分钟)

```
README.md → INSTALL.md → USAGE.md (快速开始部分) → 编写第一个程序
```

### 路径 2: 深入学习 (2 小时)

```
README.md → INSTALL.md → USAGE.md → API.md → ARCHITECTURE.md → 实践项目
```

### 路径 3: 性能优化 (1 小时)

```
PERFORMANCE.md → FAQ.md (性能部分) → 分析工具使用 → 优化实践
```

### 路径 4: 故障排查 (按需)

```
FAQ.md (故障排查部分) → USAGE.md (相关章节) → 调试工具使用
```

---

## 🔧 工具与资源

### 开发工具

- **GDB 插件**: `tools/gdb_arm2x86.py` - 调试 Arm2x86 程序
- **测试框架**: `tests/run_tests.c` - 运行单元测试
- **Docker 镜像**: `Dockerfile` - 容器化开发环境

### 在线资源

- **GitHub 仓库**: https://github.com/monkeycode-ai/arm2x86
- **问题追踪**: https://github.com/monkeycode-ai/arm2x86/issues
- **讨论区**: https://github.com/monkeycode-ai/arm2x86/discussions

---

## 📋 文档结构

```
arm2x86/
├── README.md              # 主入口
├── README_zh.md           # 中文版
├── docs/
│   ├── NAVIGATION.md      # 本文档
│   ├── INSTALL.md         # 安装指南
│   ├── USAGE.md           # 使用指南
│   ├── API.md             # API 参考
│   ├── ARCHITECTURE.md    # 架构文档
│   ├── PERFORMANCE.md     # 性能优化
│   ├── TESTING.md         # 测试指南
│   ├── CONTRIBUTING.md     # 贡献指南
│   ├── FAQ.md             # 常见问题
│   ├── CHANGELOG.md       # 变更日志
│   ├── PROJECT_PLAN.md    # 项目规划
│   └── PCACHE.md          # 持久化缓存文档
└── ...
```

---

## ❓ 常见问题

### Q: 哪个文档适合我？

**A:**
- 新用户：从 [README.md](../README.md) 开始
- 开发者：阅读 [USAGE.md](USAGE.md) 和 [API.md](API.md)
- 优化人员：查看 [PERFORMANCE.md](PERFORMANCE.md)

### Q: 如何查找特定 API 的用法？

**A:**
- 使用 [API.md](API.md) 的索引
- 或搜索代码库中的示例

### Q: 文档有中文版吗？

**A:**
- [README_zh.md](../README_zh.md) 是中文版
- 部分文档有中文翻译，大部分为英文

### Q: 发现文档错误怎么办？

**A:**
- 提交 Issue 报告问题
- 或直接提交 PR 修复

---

## 📞 获取帮助

1. **文档**: 首先查阅相关文档
2. **FAQ**: 查看常见问题解答
3. **Issue**: 在 GitHub 提交问题
4. **Discussion**: 在讨论区提问

---

## 📝 文档贡献

欢迎贡献文档！请遵循 [CONTRIBUTING.md](CONTRIBUTING.md) 中的指南。

### 可以贡献的内容

- 纠正拼写和语法错误
- 补充代码示例
- 添加新的使用场景
- 翻译文档
- 改进文档结构

---

**最后更新**: 2024-xx-xx
**维护者**: Arm2x86 Project Team
