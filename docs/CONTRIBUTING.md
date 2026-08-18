# Arm2x86 贡献指南

欢迎为 Arm2x86 项目做出贡献！本文档将帮助你快速上手。

## 目录

- [代码仓库](#代码仓库)
- [开发环境设置](#开发环境设置)
- [提交流程](#提交流程)
- [代码规范](#代码规范)
- [测试要求](#测试要求)
- [文档贡献](#文档贡献)
- [问题报告](#问题报告)
- [功能请求](#功能请求)

## 代码仓库

- **主仓库**: GitHub
- **分支策略**: 
  - `main` - 主分支，稳定版本
  - `develop` - 开发分支，新功能整合
  - `feature/*` - 功能分支
  - `fix/*` - bug 修复分支
  - `release/*` - 发布分支

## 开发环境设置

### 1. 克隆仓库

```bash
git clone https://github.com/your-username/arm2x86.git
cd arm2x86
```

### 2. 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y gcc make git

# Fedora/RHEL
sudo dnf install gcc make git

# macOS
xcode-select --install
```

### 3. 构建项目

```bash
# 标准构建
make

# 调试版本（推荐用于开发）
make debug

# 验证构建
ls -lh libarm2x86.so
```

### 4. 运行测试

```bash
# 运行所有测试
./run_tests.sh

# 运行单元测试
./run_tests.sh unit

# 运行性能测试
./run_tests.sh perf
```

## 提交流程

### 1. 创建功能分支

```bash
# 从 develop 分支创建
git checkout develop
git checkout -b feature/your-feature-name
```

### 2. 开发并提交

```bash
# 编写代码
vim modules/your_module.c

# 添加更改
git add modules/your_module.c

# 提交（遵循提交规范）
git commit -m "feat: add new SIMD instruction support"
```

### 3. 推送分支

```bash
git push origin feature/your-feature-name
```

### 4. 创建 Pull Request

- 访问 GitHub 仓库
- 点击 "New Pull Request"
- 选择你的分支
- 填写 PR 描述
- 等待 CI 检查和代码审查

### 5. 代码审查

- 回应审查意见
- 根据反馈修改代码
- 重新推送（自动更新 PR）
- 获得批准后合并

## 代码规范

### 命名规范

**文件命名**：
```
modules/arm2x86_module_name.c   - 模块实现
modules/arm2x86_module_name.h   - 模块头文件
```

**函数命名**：
```c
// 公共 API：arm2x86_ 前缀
arm2x86_init()
arm2x86_convert()

// 模块内部：模块名_ 前缀
decode_arm64()
translate_add()
emit_mov()

// 私有函数：t_ 前缀
tcache_hash()
tcache_free_entry()
```

**变量命名**：
```c
// 类型：小写 + _t 后缀
typedef struct { ... } arm2x86_context_t;

// 常量：全大写
#define ARM2X86_OK 0
#define MAX_INSTRUCTIONS 10000

// 全局变量：g_ 前缀
static int g_debug_level = 0;

// 局部变量：小写，单词间用下划线
uint8_t *x86_code = NULL;
size_t block_size;
```

### 注释规范

**文件头注释**：
```c
/* ============================================================
 * arm2x86_module.c - Module Description
 * ============================================================ */
```

**函数注释**：
```c
/**
 * Brief description
 * 
 * @param param1 Description
 * @param param2 Description
 * @return Return value description
 */
int function_name(int param1, const char *param2);
```

**行内注释**：
```c
// 使用双斜杠
uint32_t opcode = (insn >> 26) & 0x3F;  // Extract opcode bits

/* 块注释用于解释复杂逻辑 */
/*
 * ARM64 LDR 指令格式:
 * [31:30] - size
 * [29:24] - opcode
 * ...
 */
```

### 代码风格

**缩进**：4 个空格（不使用 Tab）

```c
// ✓ 正确
void function() {
    if (condition) {
        do_something();
    }
}

// ✗ 错误
void function() {
	if (condition) {  // Tab
	    do_something();  // 2 空格
	}
}
```

**空格使用**：

```c
// 运算符两侧空格
int x = a + b;

// 控制语句后空格
if (condition) { }
for (int i = 0; i < n; i++) { }

// 函数调用无空格
function(arg1, arg2);

// 指针星号跟随类型
uint8_t *ptr;
int *const p;
```

**行长度**：不超过 80 字符（特殊情况可放宽到 100）

```c
// ✓ 正确：长行拆分
int result = some_function(arg1, arg2, 
                           arg3, arg4);

// ✗ 错误：超长行
int result = some_function(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
```

### 错误处理

```c
// ✓ 正确：检查所有返回值
int rc = arm2x86_init(&ctx, path, cmd);
if (rc != ARM2X86_OK) {
    fprintf(stderr, "Init failed: %d\n", rc);
    return rc;
}

// ✗ 错误：忽略错误
arm2x86_init(&ctx, path, cmd);  // 可能失败！
do_something();
```

### 内存管理

```c
// ✓ 正确：谁分配谁释放
uint8_t *buffer = malloc(size);
if (!buffer) {
    return ARM2X86_ERR_MEMORY;
}
// 使用...
free(buffer);

// ✗ 错误：内存泄漏
uint8_t *buffer = malloc(size);
// 忘记 free
```

## 测试要求

### 单元测试

所有新功能必须包含单元测试：

```c
// tests/test_your_feature.c
int test_your_feature_basic() {
    // 设置
    arm2x86_Context ctx;
    arm2x86_init(&ctx, NULL, "test");
    
    // 执行
    int result = your_function(&ctx);
    
    // 验证
    ASSERT_EQ(EXPECTED, result, "Basic test");
    
    // 清理
    arm2x86_destroy(&ctx);
    return TEST_PASS;
}
```

### 测试覆盖率

- 新功能代码覆盖率要求：≥80%
- 关键模块覆盖率要求：≥90%

### 性能测试

性能优化类 PR 需要提供性能对比：

```markdown
## 性能提升

翻译速度：50K → 75K instr/s (+50%)
缓存命中率：70% → 85% (+15%)
内存占用：128MB → 96MB (-25%)
```

## 文档贡献

### 代码文档

- 所有公共 API 必须有完整的文档注释
- 复杂算法需要有实现说明
- 更新相关文档（API.md, ARCHITECTURE.md 等）

### 用户文档

欢迎改进以下文档：

- README.md - 项目介绍
- USAGE.md - 使用指南
- PERFORMANCE.md - 性能优化
- ARCHITECTURE.md - 架构设计
- TESTING.md - 测试指南

### 文档格式

使用 Markdown 格式，代码块指定语言：

```markdown
## 示例

```c
#include "arm2x86.h"

int main() {
    arm2x86_Context ctx;
    arm2x86_init(&ctx, NULL, "test");
    // ...
    arm2x86_destroy(&ctx);
    return 0;
}
```
```

## 问题报告

### Bug 报告模板

```markdown
**基本信息**
- Arm2x86 版本：0.1.0
- 操作系统：Ubuntu 22.04
- 编译器：GCC 11.2.0

**问题描述**
清晰简洁地描述问题

**复现步骤**
1. 步骤 1
2. 步骤 2
3. 步骤 3

**期望行为**
应该发生什么

**实际行为**
实际发生了什么

**日志输出**
```
错误日志...
```

**测试用例**
如果可以，提供最小测试用例
```

### 提交 Bug

1. 在 GitHub Issues 中搜索是否已有相同报告
2. 使用 Bug 报告模板
3. 提供足够信息以便复现
4. 如果可以，提供修复建议

## 功能请求

### 功能请求模板

```markdown
**功能描述**
清晰描述想要的功能

**使用场景**
为什么需要这个功能？解决什么问题？

**实现建议**
如果有想法，描述如何实现

**替代方案**
如果没有这个功能，目前的变通方法是什么

**额外信息**
相关的参考资料、链接等
```

## 代码审查清单

提交 PR 前自我检查：

- [ ] 代码遵循项目规范
- [ ] 添加了必要的单元测试
- [ ] 所有测试通过（本地 CI）
- [ ] 更新了相关文档
- [ ] 没有引入编译警告
- [ ] 没有内存泄漏
- [ ] 性能没有回归
- [ ] 提交信息规范

## 社区行为准则

### 我们的承诺

为了营造一个开放和友好的环境，我们承诺：

- 使用友好和包容的语言
- 尊重不同的观点和经验
- 优雅地接受建设性批评
- 关注对社区最有利的事情
- 对其他社区成员表示同理心

### 不可接受的行为

- 使用性化的语言或图像
- 人身攻击或侮辱性评论
- 公开或私下骚扰
- 未经许可发布他人信息
- 其他不道德或不专业的行为

## 联系方式

- GitHub Issues：问题讨论
- 邮件列表：开发讨论
- 讨论区：一般问题

## 许可证

通过贡献代码，你同意你的贡献遵循项目的 LGPL-3.0 许可证。

---

感谢你的贡献！Arm2x86 因为有你而更好。
