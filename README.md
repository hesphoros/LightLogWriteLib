# LightLogWrite 
实现的轻量级的日志写入功能 包含有锁实现 和 无锁实现

**LightLogWrite_Impl** 有锁实现

**LockFreeLogWriteImpl** 无锁实现

初始都给定了maxQueueSize 为 日志写入队列的大小

当达到maxQueueSize时存在以下两种方案 

1. Block 阻塞写入
2. DropOldest 丢弃旧日志

## 基本作用

`LightLogWrite_Impl` 是一个**多线程安全**的日志写入实现类，支持：
- 日志写入队列，异步落盘
- 日志文件自动分割（按 AM/PM 切分，一天两份）
- 支持多种队列满时策略（阻塞/丢弃最旧）
- 支持多种文件名/路径类型（wstring、string、u16string）
- 日志丢弃计数与上报
- 线程安全，条件变量唤醒写线程

### 日志文件名设置

- `SetLogsFileName`（支持多种字符串类型）：设置日志输出文件名，自动创建目录
- `SetLastingsLogs`：设置“持久化”日志路径和基名，调用 `CreateLogsFile` 生成实际文件

### 日志写入

- `WriteLogContent`（支持多种字符串类型）：
  - **Block策略**：队列满时阻塞等待，直到有空间
  - **DropOldest策略**：队列满时丢弃最旧日志，计数并按区间上报
  - 写入后唤醒写线程
- **丢弃上报**：报告日志队列溢出（递归调用自己，防止无限递归）

### 日志队列满时策略

- **Block**（默认）：写入线程会阻塞到队列有空间
- **DropOldest**：丢弃最旧日志，写入新日志，并统计丢弃数、周期性上报

# 性能对比

在使用4个线程同时写入 每个线程写入100 0000条日志的情况下

![image-20250527140117287](https://cdn.jsdelivr.net/gh/hesphoros/blogimages@main/img/image-20250527140117287.png)
