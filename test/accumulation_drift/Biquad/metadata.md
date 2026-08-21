# biquad_df2t_target.c 程序说明

## 1. 程序简介

`biquad_df2t_target.c` 是一个面向混合精度研究的 IIR Biquad 滤波器 C 程序。程序实现经典二阶 IIR 滤波器，即 Biquad filter，并采用 Direct Form II Transposed，简称 DF2T，结构进行递推计算。

该程序主要用于测试混合精度工具在 DSP 递归滤波场景下的有效性，尤其适合分析递归状态变量、反馈系数、乘加表达式和长期信号处理过程中浮点误差的传播。

Biquad 滤波器的传递函数为：

```text
H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
```

对应 DF2T 更新形式为：

```text
y  = b0*x + w1
w1 = b1*x - a1*y + w2
w2 = b2*x - a2*y
```

其中 `x` 是当前输入样本，`y` 是当前输出样本，`w1` 和 `w2` 是滤波器内部状态变量。

## 2. 应用领域

该程序属于以下应用领域：

```text
数字信号处理 DSP
IIR 滤波器
音频信号处理
递归系统仿真
浮点精度敏感计算
混合精度 benchmark
```

从 benchmark 分类角度看，它可以归为：

```text
recursive DSP kernel
IIR filter benchmark
stateful floating-point recurrence
feedback-sensitive numerical computation
```

该程序特别适合作为混合精度测试集中的“递归状态敏感”案例，因为输出不仅依赖当前输入，还依赖历史状态 `w1` 和 `w2`。一旦状态变量精度降低，误差可能在长时间序列处理中持续传播甚至放大。

## 3. 核心数学结构

程序实现的是二阶 IIR 滤波器：

```text
H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
```

采用 Direct Form II Transposed 结构后，每个样本的计算为：

```c
double y = b0 * x + *w1;

double feedforward_1 = b1 * x;
double feedback_1    = a1 * y;
double new_w1        = feedforward_1 - feedback_1 + *w2;

double feedforward_2 = b2 * x;
double feedback_2    = a2 * y;
double new_w2        = feedforward_2 - feedback_2;

*w1 = new_w1;
*w2 = new_w2;

return y;
```

该结构避免了直接使用历史输入和历史输出数组，而是使用两个内部状态变量 `w1` 和 `w2` 进行递推，因此具有较高的状态精度敏感性。

## 4. 程序定位

该程序是一个“目标程序版本”，用于混合精度工具链输入。它不包含高精度 reference，也不包含完整误差指标计算。程序只负责：

```text
1. 生成输入信号；
2. 初始化 Biquad 滤波器系数；
3. 执行 DF2T 滤波；
4. 统计运行时间；
5. 输出 checksum、energy、max_abs 等摘要信息。
```

正式误差验证应由外部 harness 完成，例如对比原始 baseline 输出序列和混合精度转换后输出序列之间的误差。

## 5. 核心数据结构

程序使用 `Biquad` 结构体存储滤波器系数和状态：

```c
typedef struct {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
    double w1;
    double w2;
} Biquad;
```

字段含义如下：

| 字段         | 含义                                     |
| ------------ | ---------------------------------------- |
| `b0, b1, b2` | 前馈系数                                 |
| `a1, a2`     | 反馈系数，分母为 `1 + a1*z^-1 + a2*z^-2` |
| `w1, w2`     | DF2T 内部状态变量                        |

其中 `w1` 和 `w2` 是最关键的精度敏感变量，因为它们会跨样本持续传播。

## 6. 核心函数说明

### 6.1 `biquad_df2t()`

```c
double biquad_df2t(double x,
                   double b0, double b1, double b2,
                   double a1, double a2,
                   double *w1, double *w2)
```

该函数实现单个样本的 DF2T Biquad 更新，是程序最核心的计算函数，也是混合精度工具最应该关注的目标区域。

主要计算包括：

```text
y = b0*x + w1
new_w1 = b1*x - a1*y + w2
new_w2 = b2*x - a2*y
```

其中 `new_w1` 的表达式同时包含前馈项、反馈项和历史状态项，是最容易出现抵消误差和状态传播误差的位置。

### 6.2 `process_signal()`

```c
void process_signal(const double *input, double *output, size_t nsamples, Biquad *q)
```

该函数对整段输入信号逐样本调用 `biquad_df2t()`，生成输出信号。

其循环结构为：

```c
for (size_t n = 0; n < nsamples; ++n) {
    output[n] = biquad_df2t(input[n],
                            q->b0, q->b1, q->b2,
                            q->a1, q->a2,
                            &q->w1, &q->w2);
}
```

由于 `q->w1` 和 `q->w2` 在每次迭代后都会更新，因此该循环具有明显的循环依赖，不适合简单并行化，也非常适合测试递归状态精度敏感性。

### 6.3 `init_biquad()`

```c
static void init_biquad(Biquad *q, int filter_mode, double fs)
```

该函数根据 `filter_mode` 初始化不同类型的 Biquad 滤波器系数，包括低通、高通、陷波和高 Q 带通/谐振器。

### 6.4 `fill_input()`

```c
static void fill_input(double *x, size_t nsamples, int input_mode, uint64_t seed, double fs)
```

该函数生成输入信号。不同输入模式用于覆盖不同数值场景，例如多频正弦、冲激、阶跃、小噪声、随机信号和衰减正弦。

### 6.5 `checksum_signal()`

该函数计算输出信号的加权 checksum，用于粗略判断输出是否发生明显变化。

### 6.6 `signal_energy()`

该函数计算输出信号能量：

```text
energy = Σ y[n]^2
```

能量可以作为滤波器输出整体幅度和稳定性的辅助指标。

### 6.7 `max_abs_signal()`

该函数计算输出信号最大绝对值，用于检查是否出现异常放大、发散或 NaN/Inf 前兆。

## 7. 编译方式

推荐编译命令：

```bash
gcc -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    biquad_df2t_target.c -lm -o biquad_df2t
```

或使用 Clang：

```bash
clang -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    biquad_df2t_target.c -lm -o biquad_df2t
```

参数说明：

| 参数                | 含义               |
| ------------------- | ------------------ |
| `-std=c99`          | 使用 C99 标准      |
| `-O3`               | 开启优化           |
| `-fno-fast-math`    | 禁止不安全浮点优化 |
| `-ffp-contract=off` | 禁止自动 FMA 融合  |
| `-lm`               | 链接数学库         |

对于精度研究，不建议开启：

```bash
-ffast-math
```

因为它可能改变浮点表达式重排、舍入行为和异常值语义，从而干扰混合精度搜索结果。

## 8. 运行方式

程序运行格式为：

```bash
./biquad_df2t [nsamples] [iters] [input_mode] [filter_mode] [seed]
```

默认参数为：

```text
nsamples = 1000000
iters = 3
input_mode = 0
filter_mode = 0
seed = 1
```

示例：

```bash
./biquad_df2t 1000000 3 0 0 1
```

高 Q notch 精度压力测试：

```bash
./biquad_df2t 1000000 3 2 2 1
```

高 Q band-pass / resonator 测试：

```bash
./biquad_df2t 1000000 3 4 3 1
```

## 9. 输入信号模式

程序支持 5 种输入信号模式：

| input_mode | 输入类型          | 用途                         |
| ---------- | ----------------- | ---------------------------- |
| `0`        | 多频正弦输入      | 普通 DSP/音频滤波场景        |
| `1`        | 冲激输入          | 测试冲激响应和状态衰减       |
| `2`        | 阶跃 + 小噪声     | 测试递归状态长期稳定性       |
| `3`        | `[-1,1]` 随机输入 | 宽带随机压力测试             |
| `4`        | 衰减正弦          | 测试接近谐振频率时的状态精度 |

其中：

- `input_mode=1` 适合观察滤波器自然响应；
- `input_mode=2` 适合测试长期状态稳定性；
- `input_mode=3` 适合做随机信号压力测试；
- `input_mode=4` 适合测试高 Q 滤波器或谐振器的状态误差传播。

## 10. 滤波器模式

程序支持 4 种滤波器模式：

| filter_mode | 滤波器类型                 | 数值特征                         |
| ----------- | -------------------------- | -------------------------------- |
| `0`         | 低通，moderate Q           | 普通低通滤波                     |
| `1`         | 高通，moderate Q           | 普通高通滤波                     |
| `2`         | 高 Q 窄陷波 notch          | pole-zero cancellation，精度敏感 |
| `3`         | 高 Q band-pass / resonator | 长时间 ringing，状态变量敏感     |

其中 `filter_mode=2` 和 `filter_mode=3` 更适合作为混合精度压力测试，因为它们对系数、状态变量和反馈项精度更敏感。

## 11. 输出说明

程序输出如下内容：

```text
benchmark
nsamples
iters
input_mode
filter_mode
seed
sample_rate
precision
sizeof_double
DBL_MANT_DIG
DBL_DIG
coeff_b0
coeff_b1
coeff_b2
coeff_a1
coeff_a2
final_w1
final_w2
avg_kernel_time_sec
gflops_est
checksum
energy
max_abs
```

主要字段含义如下：

| 输出项                 | 含义                  |
| ---------------------- | --------------------- |
| `coeff_b0/b1/b2/a1/a2` | 滤波器归一化系数      |
| `final_w1/final_w2`    | 处理结束后的内部状态  |
| `avg_kernel_time_sec`  | 平均处理时间          |
| `gflops_est`           | 估算 GFLOP/s          |
| `checksum`             | 输出信号加权 checksum |
| `energy`               | 输出信号能量          |
| `max_abs`              | 输出信号最大绝对值    |

`checksum`、`energy` 和 `max_abs` 只能作为摘要指标，正式精度评估建议比较完整输出序列。

## 12. FLOP 估算

程序按每个样本约 9 次浮点操作估算性能：

```text
y      = b0*x + w1         -> 1 mul + 1 add
new_w1 = b1*x - a1*y + w2  -> 2 mul + 2 add/sub
new_w2 = b2*x - a2*y       -> 2 mul + 1 sub
```

合计：

```text
5 次乘法 + 4 次加/减 = 9 FLOPs/sample
```

因此：

```text
GFLOP/s ≈ 9 * nsamples / time / 1e9
```

该指标只是估算值，主要用于对比不同精度配置下的相对性能变化。

## 13. 混合精度搜索对象

该程序适合变量级或函数级混合精度工具搜索，重点对象包括：

| 对象               | 说明           | 敏感性   |
| ------------------ | -------------- | -------- |
| `x`                | 当前输入样本   | 中等     |
| `y`                | 当前输出样本   | 高       |
| `w1`, `w2`         | 递归状态变量   | 极高     |
| `new_w1`, `new_w2` | 状态更新临时量 | 高       |
| `feedforward_1/2`  | 前馈乘积项     | 中等     |
| `feedback_1/2`     | 反馈乘积项     | 高       |
| `a1`, `a2`         | 反馈系数       | 高       |
| `b0`, `b1`, `b2`   | 前馈系数       | 中等到高 |

其中，`w1`、`w2`、`new_w1`、`new_w2`、`a1*y` 和 `a2*y` 是最值得关注的精度敏感部分。

## 14. 数值敏感性分析

Biquad IIR 滤波器是经典 DSP 精度问题，原因在于它包含递归反馈结构：

```text
当前输出 y[n] 依赖当前输入 x[n] 和历史状态；
历史状态又由之前的输出和状态更新而来。
```

因此，小的舍入误差不会只影响当前样本，还会进入 `w1/w2`，并影响后续所有样本。

### 14.1 状态变量误差传播

`w1` 和 `w2` 是内部状态变量。如果它们被降为低精度，误差会随样本递推持续传播。

### 14.2 反馈项敏感

反馈项：

```text
-a1*y
-a2*y
```

直接决定滤波器极点位置和递归稳定性。降低反馈项精度可能改变滤波器响应，尤其在高 Q 情况下更加明显。

### 14.3 抵消误差

状态更新：

```text
new_w1 = b1*x - a1*y + w2
```

可能出现前馈项、反馈项和状态项之间的抵消。低精度下，抵消后的有效位数可能显著减少。

### 14.4 高 Q 滤波器敏感性

高 Q notch 或 resonator 的极点更接近单位圆，状态衰减更慢，对精度更敏感。`filter_mode=2` 和 `filter_mode=3` 适合专门测试这一问题。

### 14.5 长时间序列敏感性

`nsamples` 越大，状态变量传播时间越长，误差越容易积累。因此建议使用较长输入序列测试混合精度配置的长期稳定性。

## 15. 推荐测试配置

普通测试：

```bash
./biquad_df2t 1000000 3 0 0 1
```

冲激响应测试：

```bash
./biquad_df2t 1000000 3 1 0 1
```

长期状态稳定性测试：

```bash
./biquad_df2t 1000000 3 2 0 1
```

高 Q notch 测试：

```bash
./biquad_df2t 1000000 3 2 2 1
```

高 Q resonator 测试：

```bash
./biquad_df2t 1000000 3 4 3 1
```

随机输入压力测试：

```bash
./biquad_df2t 1000000 3 3 3 1
```

## 16. 外部误差验证建议

由于本程序不内置 reference 和误差指标，建议外部 harness 按如下流程验证混合精度配置：

```text
1. 编译原始 double baseline；
2. 运行 baseline，保存完整输出序列 y[n]；
3. 运行工具转换后的 mixed-precision 版本；
4. 保存 mixed 输出序列；
5. 逐样本比较两者误差；
6. 检查是否满足误差阈值；
7. 比较运行时间判断是否加速。
```

推荐误差指标包括：

```text
max_abs_err
max_rel_err
rmse
rel_l2_err
energy_diff
checksum_diff
final_state_diff
```

其中 `final_w1` 和 `final_w2` 也应作为状态误差检查项，因为状态变量可能在输出误差还不明显时已经发生漂移。

## 17. 作为 benchmark 的分类建议

该程序可以归类为：

```text
DSP kernel
IIR Biquad filter
recursive floating-point recurrence
state-sensitive mixed-precision benchmark
```

在混合精度 benchmark 中，可以归入以下危险类型：

| 分类              | 说明                                     |
| ----------------- | ---------------------------------------- |
| 递归状态传播      | `w1/w2` 跨样本持续更新                   |
| 反馈敏感          | `a1*y`、`a2*y` 影响极点和稳定性          |
| 加减抵消          | `b1*x - a1*y + w2` 容易发生 cancellation |
| 长时间序列        | 误差随样本数累积                         |
| 高 Q 敏感         | 极点接近单位圆，状态衰减慢               |
| 函数/变量混合精度 | 可搜索系数、状态、乘积和输出变量         |

## 18. 注意事项

1. 不建议只用短序列测试，因为短序列可能无法暴露递归状态误差。
2. 高 Q 滤波器比普通低通/高通更适合压力测试。
3. `w1` 和 `w2` 通常不应轻易降精度。
4. 若工具允许降低函数调用精度，应谨慎处理 `sin/cos/exp` 等输入生成函数；通常建议只搜索 `biquad_df2t()` 或 `process_signal()`。
5. `checksum` 不是完整精度验证指标，正式实验应对比完整输出序列。
6. 编译时不建议使用 `-ffast-math`。
7. 如果工具改变了 `a1/a2` 或状态变量精度，应特别检查稳定性和最终状态误差。

## 19. 总结

`biquad_df2t_target.c` 是一个面向混合精度研究的经典 DSP 递归滤波 benchmark。它实现二阶 IIR Biquad 滤波器的 Direct Form II Transposed 结构，核心计算集中在 `biquad_df2t()` 和 `process_signal()`。由于包含反馈项、递归状态、抵消表达式和长时间序列误差传播，该程序非常适合测试混合精度工具是否能够识别并保护关键状态变量，同时安全降低非关键变量或操作的精度以获得加速。