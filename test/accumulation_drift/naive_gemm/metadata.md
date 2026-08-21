# gemm_naive.c 程序说明

## 1. 程序简介

`gemm_naive.c` 是一个面向混合精度调优工具链的 Naive GEMM 单文件 C 程序。该程序实现经典三重循环矩阵乘法：

```text
C = A × B
```

其中：

```text
A ∈ R^(M×K)
B ∈ R^(K×N)
C ∈ R^(M×N)
```

本程序的核心特点是：目标计算函数 `gemm_target()` 采用 `long double` 作为 baseline 精度，并显式暴露输入读取变量、乘积变量和累加变量，便于变量级或函数级混合精度搜索工具进行分析和转换。

该版本是一个纯目标程序版本，不包含高精度参考计算函数，也不包含误差指标计算函数。程序只负责生成输入数据、执行目标 GEMM、统计运行时间并输出 checksum。误差验证应由外部 harness、工具链或独立评估脚本完成。

## 2. 应用领域

该程序属于以下应用领域：

```text
稠密线性代数
矩阵乘法
Naive GEMM
浮点累加误差分析
混合精度调优 benchmark
变量级精度搜索测试程序
```

从 benchmark 分类角度看，它可以归为：

```text
dense linear algebra kernel
naive matrix multiplication
long-accumulation floating-point benchmark
long-double-to-double/float precision tuning test
```

该程序尤其适合测试混合精度工具是否能够识别 GEMM 中的关键精度敏感变量，例如累加器 `sum`，并在满足外部误差约束的前提下降低部分变量或操作的精度以获得加速。

## 3. 程序定位

该程序不是优化 GEMM，也不是 BLAS 替代品。它故意保留最朴素的三重循环结构，以便观察浮点精度转换对矩阵乘法结果和性能的直接影响。

它的主要用途包括：

1. 测试混合精度工具能否处理 `long double` baseline 程序；
2. 测试工具能否搜索局部变量的可降精度配置；
3. 测试工具能否识别长累加链中的精度敏感变量；
4. 测试从 `long double` 到 `double` 或 `float` 的降精度收益；
5. 为外部误差评估 harness 提供稳定、简单、可控的目标程序。

## 4. 核心数学计算

程序实现的数学公式为：

```text
C[i,j] = Σ A[i,k] × B[k,j],  k = 0, 1, ..., K-1
```

对应目标函数为：

```c
void gemm_target(int M, int N, int K,
                 const long double * restrict A,
                 const long double * restrict B,
                 long double * restrict C) {
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            long double sum = 0.0L;

            for (int k = 0; k < K; ++k) {
                long double a = A[(size_t)i * (size_t)K + (size_t)k];
                long double b = B[(size_t)k * (size_t)N + (size_t)j];
                long double prod = a * b;
                sum += prod;
            }

            C[(size_t)i * (size_t)N + (size_t)j] = sum;
        }
    }
}
```

该函数是程序唯一的核心计算区域，也是混合精度工具应重点分析和转换的对象。

## 5. 为什么使用 long double baseline

本版本使用 `long double` 作为目标程序 baseline，适合测试以下类型的精度调优：

```text
long double -> double
long double -> float
局部变量 long double -> double
局部乘法 long double -> double/float
累加器 long double -> double/float
输出存储 long double -> double/float
```

需要注意的是，`long double` 在不同平台上的底层实现不同：

| 平台           | 常见 long double 实现                             | 实验含义                     |
| -------------- | ------------------------------------------------- | ---------------------------- |
| x86-64         | x87 80-bit extended precision，通常占 16 字节存储 | 有硬件或半硬件支持           |
| AArch64 / 鲲鹏 | IEEE binary128 / LLVM fp128                       | 通常是软件四精度，可能非常慢 |
| 某些平台       | 与 double 相同                                    | 没有额外精度收益             |

因此，程序会输出：

```text
sizeof_long_double
LDBL_MANT_DIG
LDBL_DIG
```

用于记录当前平台上 `long double` 的实际格式，避免跨平台实验时误读性能和精度结果。

## 6. 程序结构

程序主要由以下部分组成：

```text
1. 参数解析
2. 输入矩阵生成
3. gemm_target() 目标计算
4. checksum 计算
5. 运行时间统计
6. 结果输出
```

与之前的完整实验版本不同，本程序已删除：

```text
gemm_reference_long_double()
compute_error_metrics()
高精度参考输出
max_abs_err / max_rel_err / rmse / rel_l2_err 误差计算
```

因此，程序本身不判断精度是否满足要求。混合精度配置是否正确，需要由外部工具链根据 baseline 输出或单独 reference 程序进行判断。

## 7. 编译方式

推荐使用以下方式编译：

```bash
gcc -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    gemm_naive.c -lm -o gemm_ld_target
```

或使用 Clang：

```bash
clang -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    gemm_naive.c -lm -o gemm_ld_target
```

参数说明：

| 编译参数            | 含义               |
| ------------------- | ------------------ |
| `-std=c99`          | 使用 C99 标准      |
| `-O3`               | 开启优化           |
| `-fno-fast-math`    | 禁止不安全浮点优化 |
| `-ffp-contract=off` | 禁止自动 FMA 融合  |
| `-lm`               | 链接数学库         |

对于混合精度研究，不建议使用：

```bash
-ffast-math
```

因为它可能改变浮点表达式重排、舍入行为、NaN/Inf 语义和乘加融合策略，从而干扰精度搜索结果。

## 8. 运行方式

程序运行格式为：

```bash
./gemm_ld_target [M] [N] [K] [iters] [seed] [data_mode]
```

参数含义如下：

| 参数        | 含义                            |
| ----------- | ------------------------------- |
| `M`         | 矩阵 A 和 C 的行数              |
| `N`         | 矩阵 B 和 C 的列数              |
| `K`         | 矩阵 A 的列数 / B 的行数        |
| `iters`     | GEMM 重复执行次数，用于稳定计时 |
| `seed`      | 输入数据生成种子                |
| `data_mode` | 输入数据模式                    |

默认参数为：

```text
M = 128
N = 128
K = 128
iters = 3
seed = 1
data_mode = 0
```

运行示例：

```bash
./gemm_ld_target 128 128 128 3 1 0
```

抵消误差压力测试：

```bash
./gemm_ld_target 128 128 512 3 1 2
```

动态范围测试：

```bash
./gemm_ld_target 128 128 256 3 1 3
```

## 9. 输入数据模式

程序支持 5 种输入数据模式：

| data_mode | 含义               | 用途                        |
| --------- | ------------------ | --------------------------- |
| `0`       | `[-1, 1]` 均匀随机 | 普通正负随机输入            |
| `1`       | `[0, 1]` 均匀随机  | 同号累加，较少 cancellation |
| `2`       | 交替符号模式       | 强调正负抵消误差            |
| `3`       | 宽动态范围随机值   | 强调大小量混合累加误差      |
| `4`       | 确定性平滑模式     | 便于回归测试和复现实验      |

其中：

- `data_mode=2` 适合测试工具是否会错误降低累加器精度；
- `data_mode=3` 适合测试工具对动态范围敏感计算的处理能力；
- `data_mode=4` 适合做稳定的回归测试。

## 10. 输出说明

程序输出内容包括：

```text
benchmark
M
N
K
iters
seed
data_mode
target
sizeof_long_double
LDBL_MANT_DIG
LDBL_DIG
avg_kernel_time_sec
gflops
checksum
```

各字段含义如下：

| 输出项                | 含义                         |
| --------------------- | ---------------------------- |
| `benchmark`           | benchmark 名称               |
| `M/N/K`               | GEMM 矩阵维度                |
| `iters`               | 重复执行次数                 |
| `seed`                | 输入数据种子                 |
| `data_mode`           | 输入数据模式                 |
| `target`              | 当前目标程序类型             |
| `sizeof_long_double`  | `long double` 存储大小       |
| `LDBL_MANT_DIG`       | `long double` 有效尾数位数   |
| `LDBL_DIG`            | `long double` 十进制有效数字 |
| `avg_kernel_time_sec` | 平均 GEMM 运行时间           |
| `gflops`              | GEMM 性能                    |
| `checksum`            | 输出矩阵的加权 checksum      |

`checksum` 用于帮助判断输出是否发生明显变化，但它不是完整误差指标。正式精度验证应由外部 harness 计算最大误差、相对误差或 L2 误差。

## 11. Checksum 设计

程序使用加权 checksum：

```c
checksum += C[i] * weight;
```

其中：

```c
weight = 1.0L + (long double)(i % 97) * 1.0e-4L;
```

相比简单求和，加权 checksum 能降低正负误差完全抵消的概率。但它仍然只能作为粗粒度输出摘要，不能替代完整的逐元素误差检查。

## 12. 核心热点函数

程序的唯一核心热点函数是：

```c
gemm_target()
```

运行时间主要集中在内层循环：

```c
for (int k = 0; k < K; ++k) {
    long double a = A[i*K + k];
    long double b = B[k*N + j];
    long double prod = a * b;
    sum += prod;
}
```

其计算复杂度为：

```text
O(M × N × K)
```

理论浮点操作数约为：

```text
2 × M × N × K
```

其中每个内层循环包含一次乘法和一次加法。

## 13. 混合精度工具的搜索对象

该程序特意显式拆分了变量：

```c
long double a;
long double b;
long double prod;
long double sum;
```

目的是方便工具识别变量级精度配置。

可搜索对象包括：

| 对象           | 含义         | 搜索价值                |
| -------------- | ------------ | ----------------------- |
| `A` / `B` load | 输入矩阵读取 | 测试输入精度可否降低    |
| `a` / `b`      | 局部输入变量 | 测试局部变量精度影响    |
| `prod`         | 乘积临时变量 | 测试乘法结果精度影响    |
| `sum`          | 累加器       | GEMM 最关键精度敏感变量 |
| `C` store      | 输出写回     | 测试输出存储精度影响    |

通常情况下，`sum` 是最敏感的变量，因为每个 `C[i,j]` 都包含长度为 `K` 的长累加链。

## 14. 数值敏感性

Naive GEMM 的主要数值风险来自浮点长累加：

```text
C[i,j] = A[i,0]B[0,j] + A[i,1]B[1,j] + ... + A[i,K-1]B[K-1,j]
```

随着 `K` 增大，累加误差通常会增加。

主要敏感点包括：

### 14.1 长累加链

每个输出元素需要执行 `K` 次乘加。`K` 越大，累加误差越容易积累。

### 14.2 加法非结合性

浮点加法不满足严格结合律。虽然本程序固定累加顺序为 `k=0..K-1`，但不同精度配置会改变舍入行为。

### 14.3 Cancellation

当乘积项正负交替时，可能发生严重抵消。此时如果 `sum` 精度不足，最终结果的相对误差可能明显放大。

### 14.4 宽动态范围

当输入元素数量级差异很大时，小量项可能在累加过程中被大量项吞没。

### 14.5 输出舍入

如果工具将 `C` 的存储精度降低，即使中间计算保持高精度，最终写回也可能引入明显舍入误差。

## 15. 推荐测试方式

建议将该程序作为工具链输入程序，由工具生成不同混合精度配置，然后使用外部脚本比较 baseline 输出和 tuned 输出。

推荐测试组合：

### 普通随机测试

```bash
./gemm_ld_target 128 128 128 3 1 0
```

### 长累加链测试

```bash
./gemm_ld_target 128 128 1024 3 1 0
```

### Cancellation 测试

```bash
./gemm_ld_target 128 128 512 3 1 2
```

### 宽动态范围测试

```bash
./gemm_ld_target 128 128 256 3 1 3
```

### 回归测试

```bash
./gemm_ld_target 128 128 128 3 1 4
```

## 16. 外部误差验证建议

由于本版本不包含 reference 和误差计算，建议外部 harness 做如下流程：

```text
1. 编译原始 long double baseline 程序；
2. 运行 baseline，保存输出矩阵或完整结果；
3. 运行工具生成的 mixed-precision 版本；
4. 比较两者输出；
5. 检查误差是否满足阈值；
6. 比较运行时间，判断是否加速。
```

如果不保存完整矩阵，只比较 checksum，则只能进行粗略检查。正式实验建议输出完整 `C` 矩阵或由工具链内部直接拦截矩阵结果进行误差计算。

推荐误差指标包括：

```text
max_abs_err
max_rel_err
rmse
rel_l2_err
checksum_diff
```

## 17. 作为 benchmark 的分类建议

该程序可以分类为：

```text
Dense linear algebra kernel
Naive GEMM
Long double baseline
Long accumulation precision benchmark
Variable-level mixed-precision search target
```

在混合精度 benchmark 中，可以归入以下危险类型：

| 分类         | 说明                             |
| ------------ | -------------------------------- |
| 长累加链     | 每个输出元素包含 K 次乘加        |
| 累加器敏感   | `sum` 精度直接决定最终误差       |
| Cancellation | 正负乘积抵消导致误差放大         |
| 动态范围敏感 | 大小量混合导致小量丢失           |
| 输出舍入敏感 | 降低 C 存储精度会影响最终结果    |
| 平台相关性   | `long double` 实现依赖硬件和 ABI |

## 18. 注意事项

1. `long double` 的实际实现依赖平台，必须记录 `sizeof_long_double`、`LDBL_MANT_DIG` 和 `LDBL_DIG`。
2. 在 AArch64/鲲鹏平台上，`long double` 可能对应软件 `fp128`，降为 `double` 后可能获得很大加速。
3. 这种加速应解释为 extended precision 降精度收益，而不是普通 double-to-float 混合精度收益。
4. 本程序不包含误差检查，不能单独判断 tuned 配置是否正确。
5. 对于正式实验，应配合外部 reference 或 baseline 输出比较。
6. 不建议开启 `-ffast-math`，否则可能引入额外浮点语义变化。
7. 若工具会搜索函数调用，建议谨慎处理 `powl/sinl/cosl` 等输入生成函数；通常应只搜索 `gemm_target()`。

## 19. 总结

`gemm_naive.c` 是一个极简 long double baseline Naive GEMM 程序，专门用于混合精度工具链的变量级或函数级精度搜索测试。程序只保留目标计算函数 `gemm_target()`、输入生成、计时和 checksum 输出，不包含 reference 和误差指标计算。其核心价值在于清晰暴露 GEMM 中的输入变量、乘积变量、累加器和输出变量，使工具能够搜索从 `long double` 到 `double` 或 `float` 的混合精度配置，并通过外部 harness 验证误差和加速效果。