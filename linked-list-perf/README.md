# 循环双向链表搜索与 Linux PMU 计数

`InitLink` 创建自循环头结点，`AppendNode` 利用 `head->prev` 在 O(1) 时间内追加，`FindNode` 从表头顺序遍历，先比较长度，再用 `memcmp` 比较数据。搜索无需哈希，最坏遍历 n 个结点，额外空间为 O(1)。每个结点及其数据一次分配，分配请求大小为 32–512 字节。多个链表可独立创建，程序按需要分配节点。初始化和释放在测量之外。

## 编译和运行

在可以访问硬件 PMU 的 Linux 环境运行。课程提供的实验目录为 `/home/assit/hk2`，将源码放入该目录。需要 `make`、GCC 和 Linux UAPI 开发头文件，默认使用 `-O2`。

```sh
cd /home/assit/hk2
make
./list-perf 100000 500 tail 32
./list-perf 100000 500 miss 32
./list-perf 100000 500 head 32
```

上述三组分别预期命中 500、0、500 次，访问结点 50,000,000、50,000,000、500 次。结点访问量由工作负载精确推得，与硬件访存计数是两个指标。`32` 是每个节点的数据字节数，可调整。重复查找、数据大小和节点布局都会影响缓存表现。

| 字段 | 统计口径 |
| --- | --- |
| `Instructions` | 用户态退休指令数 |
| `Cycles` | 用户态 CPU 周期数 |
| `Memory accesses` | 默认采用通用 L1D load/store access 计数。仅支持读事件时明确标为 `loads only`。可选用 CPU 专属事件统计退休的 load + store |
| `L1D load hit rate` | `(L1D load accesses - L1D load misses) / L1D load accesses`。两个读事件同组调度，统计的是 L1 数据缓存读命中率 |

默认计数范围是当前线程的用户态搜索循环以及少量计数器启停开销。计数器发生时间复用时，程序按 `raw × time_enabled / time_running` 估算，并显示实际调度比例。出现 `N/A` 时，查看标准错误中的事件打开失败信息。权限问题请使用老师提供的实验环境或由管理员授予 PMU 访问权限。若该机器或虚拟机没有暴露相应事件，请换用有相应 PMU 的 Linux 主机。

如果老师要求访存以退休指令为准，可以传入当前 CPU 型号的两个原始事件编号。以下示例只适用于 Intel Skylake 对应事件定义，其他 CPU 请先用 `perf list --details` 或厂商表格确认编码。

```sh
./list-perf 100000 500 tail 32 --raw-mem 0x81d0 0x82d0
```

在课程目录将实测输出写入按日期命名的日志，例如 11 月 26 日的第一份为 `1126_1.log`。

```sh
{ uname -sr; lscpu; ./list-perf 100000 500 tail 32; } 2>&1 | tee "$(date +%m%d)_1.log"
```

## 参考资料

- [Linux perf_event_open(2)](https://man7.org/linux/man-pages/man2/perf_event_open.2.html)
- [Intel Skylake core PMU 事件表](https://github.com/intel/perfmon/blob/main/SKL/events/skylake_core.json)
