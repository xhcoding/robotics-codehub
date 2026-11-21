# 主机端逻辑测试（Uart / fmt / dbg）

在 PC 上编译**真实的** `common/peripheral/uart.cpp`、`common/debug/fmt.cpp`、
`common/debug/dbg.cpp`（配一份最小 mock HAL：`include/stm32f1xx_hal.h` + `mock_hal.cpp`），
不烧板子就能验证易错的纯逻辑。四个测试都可被 `ctest` 跑到。

## `uart_host_test` — stm32::Uart

- 纯 IT 路径：RXNE/IDLE 帧边界、帧跨接收环末尾拆两段、环满丢弃计数、错误标志计数、
  TXE 逐字节发送（含跨发送环末尾）、`flush()` 的 TC 语义
- DMA + 空闲线：每帧一次事件、事件后自动重装载、整缓冲满（pos == size）边界
- DMA 循环接收：IDLE/HT/TC 混合事件下的绝对游标账目、跨圈、消费跟不上时丢最旧并计数
- DMA 发送：按环回绕拆块（6 + 14 这类）、TxCplt 续泵、泵运行中不重复启动、
  错误中止后（HAL 无 TxCplt 回调）的 `txRescue()` 自救
- 随机压力测试：与独立参考模型逐字节对照（IT 收 500 轮 / IT 发 1500 步 / 循环 DMA 400 段）
- 配置校验与生命周期：非法配置 `panic(0x6001)`、重复实例 `panic(0x6002)`、
  析构关时钟/关中断/中止 DMA、时钟与 NVIC 使能次数、引脚复用选择

## `fmt_host_test` / `fmt_host_test_O2` — 自实现格式化器（差分测试）

把 `stm32::fmt` 和主机 libc 的 `snprintf` **逐字节对照**（含返回值语义），
5.7 万+ 组组合：12 种标志 × 宽度 0..9 × 精度 -1..7 × 边界值（0/1/INT_MIN/INT_MAX/UINT_MAX/
0xdeadbeef）× 转换 `d u x X o s`，另加 `%%`、64 位长度修饰、`%s` 传 nullptr、截断。

浮点部分做 3.6 万组随机对照，要求与 libc 一致，只允许两类已文档化的偏离：

- 恰好 `.5` 的舍入方式不同（libc half-even，本实现 half-up）
- `|%v| >= 1e15` 时 `%f` 输出 `ovf`（定点范围外宁缺勿错）；`%e/%g/%a` 输出 `(nof)`
  但仍消费 double 实参

`_O2` 那遍不带 sanitizer，用来确认优化后行为不变。

## `dbg_host_test` / `dbg_host_test_L0` / `dbg_host_test_L2` — 调试打印层

真 `dbg.cpp` 跑在真 `Uart`（纯 IT 配置）+ mock HAL 上：绑定/未绑定、发出的字节序列逐字节
比对、超长行截断（返回值 = 应写长度，实写 `DBG_LINE_SIZE-1`）、`dump()` 的排版、
在帧回调（ISR 上下文）里打印。三个变体分别按 `DBG_LEVEL=4/0/2` 编译，
其中 L0 验证等级宏把 `LOG_*` 与 `dump` 整条编译掉后确实没有任何输出。

## 运行

```sh
cmake -S embedded/stm32/tests/host_uart -B build/host_uart
cmake --build build/host_uart
ctest --test-dir build/host_uart --output-on-failure
```

约定与坑：

- `System::panic` 在测试里实现为 `throw`（交叉编译时是死循环），用于断言错误路径。
- 用 ASan/UBSan，需要 GCC/Clang。
- 只在构造 `Uart` **之前**做 mock 的整块复位；之后再 `memset(g_usart)` 会抹掉驱动写进
  `CR1` 的 `UE/TE/RE/RXNEIE/IDLEIE`，表现是"收不到任何东西"。测试里用 `clearFlags()`
  只清 `SR/DR`。
- mock 的位定义只为互不冲突而简化，不是硅片精确值：这里测的是驱动逻辑，不是 HAL 语义。
