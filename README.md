# PostgreSQL 内核课程课堂作业

- [`assignments/`](assignments/)：按原题单整理的五份 Word 作答，覆盖第 1–7 课。
- [`linked-list-perf/`](linked-list-perf/)：第 1–2 课循环双向链表源码及实验说明。使用 `perf_event_open()` 统计指令数、周期数、访存次数及 L1 数据缓存读命中率。

硬件计数需在能访问 PMU 的 Linux 实验环境实测。编译、运行及保存日志的命令见[实验说明](linked-list-perf/README.md)。
