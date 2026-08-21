# lms_adaptive_filter_target.c 程序说明

## 1. 程序简介

`lms_adaptive_filter_target.c` 是一个面向混合精度研究的 LMS 自适应滤波器 C 程序。LMS，全称 Least Mean Squares，是数字信号处理中最经典的自适应滤波算法之一，常用于回声消除、噪声对消、系统辨识和自适应均衡等任务。

该程序实现了一个自适应 FIR 滤波器。程序首先构造一个未知 FIR 系统，然后生成输入信号和期望输出信号，之后使用 LMS 算法逐样本更新滤波器权重，使自适应滤波器逐渐逼近未知系统。

LMS 的核心更新公式为：

```text
y[n] = Σ w[i] * x_vec[i]

e[n] = d[n] - y[n]

w[i] = w[i] + 2 * mu * e[n] * x_vec[i]
```

其中，`x_vec` 是当前输入延迟线，`w` 是自适应滤波器权重，`d[n]` 是期望信号，`y[n]` 是滤波器输出，`e[n]` 是误差信号，`mu` 是步长参数。

## 2. 应用领域

该程序属于以下应用领域：

```text
数字信号处理 DSP
自适应滤波
回声消除
噪声对消
系统辨识
在线学习型递推算法
混合精度 benchmark
```

从 benchmark 分类角度看，该程序可以归类为：

```text
adaptive DSP kernel
LMS adaptive filter
recursive weight-update benchmark
stateful signal-processing workload
precision-sensitive convergence benchmark
```

该程序的价值不在于单次计算复杂，而在于它具有长期递推更新过程。权重 `w[i]` 会在每个样本上被更新，误差、步长和输入向量共同决定收敛过程。因此，它非常适合测试混合精度工具是否能够识别递推状态变量、累加器和步长参数的精度敏感性。

## 3. 核心数学结构

LMS 自适应滤波包含两个核心阶段。

第一阶段是预测输出：

```text
y[n] = Σ w[i] * x_vec[i]
```

对应代码中的 `lms_predict()`：

```c
double lms_predict(const double * restrict w,
                   const double * restrict x_vec,
                   int N) {
    double acc = 0.0;

    for (int i = 0; i < N; ++i) {
        double wi = w[i];
        double xi = x_vec[i];
        double product = wi * xi;
        acc += product;
    }

    return acc;
}
```

第二阶段是权重更新：

```text
w[i] = w[i] + 2 * mu * e * x_vec[i]
```

对应代码中的 `lms_update()`：

```c
void lms_update(double * restrict w,
                const double * restrict x_vec,
                double e,
                double mu,
                int N) {
    double scale = 2.0 * mu * e;

    for (int i = 0; i < N; ++i) {
        double xi = x_vec[i];
        double delta = scale * xi;
        w[i] += delta;
    }
}
```

其中 `mu` 是 LMS 的学习率或步长。`mu` 的精度和数值大小会直接影响收敛速度、稳态误差和数值稳定性。

## 4. 程序定位

该程序是一个“目标程序版本”，适合作为混合精度工具链的输入程序。它不包含高精度 reference，也不包含完整逐元素误差计算。程序自身主要负责：

```text
1. 生成输入信号；
2. 生成未知系统 h_true；
3. 根据未知系统生成期望信号 desired；
4. 执行 LMS 自适应滤波；
5. 统计运行时间；
6. 输出 MSE、checksum、能量、最终权重误差等摘要指标。
```

正式误差验证应由外部 harness 完成，例如比较原始 double baseline 与混合精度转换后版本的完整输出序列、误差信号和最终权重。

## 5. 程序主要流程

程序整体流程如下：

```text
1. 解析命令行参数；
2. 分配 input、desired、output、error、w、x_vec、h_true；
3. 根据 input_mode 生成输入信号；
4. 根据 system_mode 生成未知 FIR 系统 h_true；
5. 使用 h_true 和 input 生成期望信号 desired；
6. 初始化自适应权重 w 和延迟线 x_vec；
7. 执行 lms_process()；
8. 统计运行时间；
9. 计算 MSE、checksum、energy、weight_error_l2；
10. 输出结果并释放内存。
```

核心处理循环位于 `lms_process()`：

```c
for (size_t n = 0; n < nsamples; ++n) {
    for (int i = N - 1; i > 0; --i) {
        x_vec[i] = x_vec[i - 1];
    }
    x_vec[0] = input[n];

    double y = lms_predict(w, x_vec, N);
    double e = desired[n] - y;

    lms_update(w, x_vec, e, mu, N);

    output[n] = y;
    error[n] = e;
}
```

这个循环具有明显的递推状态特征：`x_vec` 是输入延迟线，`w` 是随时间更新的自适应权重。后续样本的输出依赖之前样本更新后的权重，因此该程序非常适合测试长期误差传播。

## 6. 核心函数说明

### 6.1 `lms_predict()`

`lms_predict()` 计算当前自适应滤波器输出：

```text
y = Σ w[i] * x_vec[i]
```

该函数本质上是一个 dot product。其精度敏感点包括：

```text
acc
product
wi
xi
```

其中 `acc` 是累加器。对于较长滤波器，例如 `ntaps=128` 或更大，累加误差会随 tap 数增加。

### 6.2 `lms_update()`

`lms_update()` 执行 LMS 权重更新：

```text
w[i] += 2 * mu * e * x_vec[i]
```

该函数是程序中最重要的自适应更新核。其精度敏感点包括：

```text
mu
e
scale = 2 * mu * e
delta = scale * x_vec[i]
w[i]
```

其中 `w[i]` 是长期状态变量，任何舍入误差都可能进入后续所有样本的计算。

### 6.3 `lms_process()`

`lms_process()` 是完整 LMS 处理循环，负责更新延迟线、计算输出、计算误差并更新权重。

该函数同时包含：

```text
输入状态 x_vec 的递推更新；
权重状态 w 的递推更新；
误差 e 的逐样本反馈；
输出 output 和 error 的记录。
```

如果混合精度工具支持函数级或循环级分析，`lms_process()` 是核心搜索区域之一。

### 6.4 `fill_input()`

该函数根据 `input_mode` 生成输入信号。不同输入模式用于测试不同数值场景，例如白噪声、多频正弦、相关输入、阶跃信号和瞬态信号。

### 6.5 `init_unknown_system()`

该函数根据 `system_mode` 生成未知 FIR 系统 `h_true`。LMS 算法的目标是通过自适应更新，使权重 `w` 逐渐逼近 `h_true`。

### 6.6 `generate_desired_signal()`

该函数使用 `input` 和 `h_true` 生成期望信号 `desired`，相当于模拟一个未知系统的输出：

```text
desired[n] = Σ h_true[i] * x_vec[i] + small_noise
```

### 6.7 `summarize_result()`

该函数计算输出摘要，包括：

```text
mse
checksum_y
checksum_e
output_energy
error_energy
weight_l2
weight_error_l2
max_abs_error
```

这些指标用于粗略判断收敛效果和输出稳定性。

## 7. 编译方式

推荐使用以下方式编译：

```bash
gcc -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    lms_adaptive_filter_target.c -lm -o lms_filter
```

或使用 Clang：

```bash
clang -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    lms_adaptive_filter_target.c -lm -o lms_filter
```

参数说明：

| 参数                | 含义               |
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

因为它可能改变浮点表达式重排、舍入行为和异常值语义，从而干扰精度搜索结果。

## 8. 运行方式

程序运行格式为：

```bash
./lms_filter [nsamples] [ntaps] [iters] [input_mode] [system_mode] [mu] [seed]
```

默认参数为：

```text
nsamples = 200000
ntaps = 64
iters = 3
input_mode = 0
system_mode = 0
mu = 0.01
seed = 1
```

示例：

```bash
./lms_filter 200000 64 3 0 0 0.01 1
```

更长序列测试：

```bash
./lms_filter 500000 64 3 0 0 0.01 1
```

相关输入收敛压力测试：

```bash
./lms_filter 500000 128 3 2 2 0.002 1
```

交替符号系统测试：

```bash
./lms_filter 300000 128 3 0 2 0.005 1
```

## 9. 输入信号模式

程序支持 5 种输入模式：

| input_mode | 输入类型              | 用途                   |
| ---------- | --------------------- | ---------------------- |
| `0`        | `[-1,1]` 白噪声       | 标准 LMS 系统辨识      |
| `1`        | 多频正弦              | 音频/周期信号场景      |
| `2`        | colored AR-like input | 相关输入，收敛更困难   |
| `3`        | step + small noise    | 长期状态和步长敏感测试 |
| `4`        | impulse + 衰减正弦    | 瞬态响应测试           |

其中，`input_mode=2` 适合测试相关输入情况下的 LMS 收敛问题，因为相关输入会增加自适应滤波器的收敛难度。

## 10. 未知系统模式

程序支持 4 种未知系统模式：

| system_mode | 未知系统类型     | 数值特征            |
| ----------- | ---------------- | ------------------- |
| `0`         | sparse echo path | 回声消除典型场景    |
| `1`         | 指数衰减 FIR     | 平滑系统辨识        |
| `2`         | 交替符号 FIR     | cancellation 更明显 |
| `3`         | 伪随机 FIR       | 一般随机系统辨识    |

其中：

- `system_mode=0` 适合模拟回声路径；
- `system_mode=2` 适合测试加减抵消和权重收敛精度；
- `system_mode=3` 适合做一般随机系统辨识测试。

## 11. 输出说明

程序输出如下字段：

```text
benchmark
nsamples
ntaps
iters
input_mode
system_mode
seed
sample_rate
mu
precision
sizeof_double
DBL_MANT_DIG
DBL_DIG
avg_kernel_time_sec
gflops_est
mse
max_abs_error
checksum_y
checksum_e
output_energy
error_energy
weight_l2
weight_error_l2
```

各字段含义如下：

| 输出项                | 含义                                       |
| --------------------- | ------------------------------------------ |
| `mse`                 | 输出误差的均方值                           |
| `max_abs_error`       | 误差信号最大绝对值                         |
| `checksum_y`          | 输出信号加权 checksum                      |
| `checksum_e`          | 误差信号加权 checksum                      |
| `output_energy`       | 输出信号能量                               |
| `error_energy`        | 误差信号能量                               |
| `weight_l2`           | 最终自适应权重的 L2 范数                   |
| `weight_error_l2`     | 最终权重与真实系统 `h_true` 之间的 L2 误差 |
| `avg_kernel_time_sec` | 平均运行时间                               |
| `gflops_est`          | 估算 GFLOP/s                               |

其中 `mse` 和 `weight_error_l2` 是最重要的两个指标。`mse` 反映输出误差，`weight_error_l2` 反映 LMS 是否成功学习到未知系统。

## 12. FLOP 估算

程序按每个样本约 `4N + 3` 次浮点操作估算性能：

```text
prediction:
  N 次乘法 + N 次加法 ≈ 2N FLOPs

error:
  e = d - y ≈ 1 FLOP

update:
  scale = 2 * mu * e ≈ 2 FLOPs
  每个 tap: delta = scale*x[i], w[i] += delta ≈ 2N FLOPs

total:
  2N + 1 + 2 + 2N = 4N + 3 FLOPs/sample
```

因此：

```text
GFLOP/s ≈ (4N + 3) * nsamples / time / 1e9
```

该指标主要用于比较不同精度配置下的相对性能，而不是作为严格硬件峰值性能评价。

## 13. 混合精度搜索对象

该程序适合变量级和函数级混合精度工具搜索。重点对象包括：

| 对象       | 含义               | 敏感性 |
| ---------- | ------------------ | ------ |
| `w[i]`     | 自适应滤波器权重   | 极高   |
| `x_vec[i]` | 输入延迟线         | 中到高 |
| `acc`      | dot product 累加器 | 高     |
| `product`  | 单项乘积           | 中     |
| `y`        | 当前输出           | 高     |
| `e`        | 当前误差           | 极高   |
| `mu`       | LMS 步长           | 极高   |
| `scale`    | `2*mu*e`           | 高     |
| `delta`    | 权重更新量         | 高     |

其中，`mu`、`e`、`scale`、`delta` 和 `w[i]` 是最需要谨慎处理的变量。降低这些变量的精度可能直接改变 LMS 的收敛路径。

## 14. 数值敏感性分析

LMS 自适应滤波器具有典型的递推状态敏感性。

### 14.1 权重递推误差

权重更新公式为：

```text
w[i] = w[i] + 2 * mu * e * x[i]
```

权重 `w[i]` 是长期状态变量。如果更新量 `delta` 的精度不足，误差会累积到权重中，并影响后续所有样本的预测输出。

### 14.2 步长 `mu` 敏感性

`mu` 控制收敛速度和稳定性。过大的 `mu` 可能导致发散，过小的 `mu` 会导致收敛缓慢。若 `mu` 或 `scale = 2*mu*e` 精度不足，可能改变收敛曲线。

### 14.3 误差信号敏感性

误差为：

```text
e = desired[n] - y
```

如果 `desired[n]` 与 `y` 接近，会发生 cancellation。低精度可能导致误差信号失真，从而影响权重更新方向和幅度。

### 14.4 Dot Product 累加误差

预测输出 `y` 是长度为 `N` 的点积。随着 `ntaps` 增大，累加误差会增加，尤其在交替符号系统或相关输入下更加明显。

### 14.5 长时间收敛敏感性

LMS 算法需要大量样本逐步收敛。短时间内不明显的误差可能在长期训练中逐渐改变最终权重和稳态误差。

## 15. 推荐测试配置

普通回声路径测试：

```bash
./lms_filter 200000 64 3 0 0 0.01 1
```

更长序列测试：

```bash
./lms_filter 500000 64 3 0 0 0.01 1
```

相关输入测试：

```bash
./lms_filter 500000 128 3 2 0 0.002 1
```

交替符号系统测试：

```bash
./lms_filter 300000 128 3 0 2 0.005 1
```

小步长收敛测试：

```bash
./lms_filter 500000 128 3 0 3 0.001 1
```

阶跃输入稳定性测试：

```bash
./lms_filter 300000 64 3 3 0 0.001 1
```

## 16. 外部误差验证建议

由于本程序不内置高精度 reference，建议外部 harness 按如下流程验证混合精度配置：

```text
1. 编译并运行原始 double baseline；
2. 保存 output、error 和最终权重 w；
3. 编译并运行 mixed-precision 转换版本；
4. 比较两者的完整输出序列；
5. 比较两者的误差信号；
6. 比较最终权重差异；
7. 检查 MSE、weight_error_l2 和运行时间。
```

推荐误差指标包括：

```text
max_abs_output_err
max_rel_output_err
rmse_output
rel_l2_output
mse_diff
weight_l2_diff
weight_error_l2_diff
checksum_y_diff
checksum_e_diff
```

对于 LMS，最终权重误差非常重要。某些配置可能短期输出误差不大，但最终权重已经明显偏离真实系统。

## 17. 作为 benchmark 的分类建议

该程序可以归类为：

```text
DSP adaptive filtering kernel
LMS adaptive filter
recursive weight-update benchmark
convergence-sensitive mixed-precision workload
```

在混合精度 benchmark 中，可以归入以下危险类型：

| 分类           | 说明                                        |
| -------------- | ------------------------------------------- |
| 长期递推状态   | 权重 `w[i]` 会跨样本持续更新                |
| 步长敏感       | `mu` 影响收敛速度和稳定性                   |
| 点积累加       | `lms_predict()` 包含长度为 N 的 dot product |
| Cancellation   | `e = desired - y` 可能发生抵消              |
| 收敛路径敏感   | 微小误差可能改变最终权重                    |
| 反馈式学习过程 | 当前误差决定下一步权重更新                  |

## 18. 注意事项

1. `mu` 的选择非常关键，不同输入和系统模式下稳定范围不同。
2. 不建议只用短序列测试混合精度配置，因为短序列可能无法暴露收敛误差。
3. `w[i]`、`e`、`mu` 和 `scale` 通常不应轻易降精度。
4. 若工具会搜索函数调用，应重点限制搜索范围到 `lms_predict()`、`lms_update()` 和 `lms_process()`。
5. `checksum` 不是完整误差指标，正式实验应比较完整输出序列和最终权重。
6. 不建议使用 `-ffast-math`，否则可能改变浮点语义。
7. 相关输入和交替符号系统比白噪声输入更适合作为压力测试。

## 19. 总结

`lms_adaptive_filter_target.c` 是一个面向混合精度研究的 LMS 自适应滤波 benchmark。它实现了经典的预测误差计算和权重更新公式，核心热点函数为 `lms_predict()`、`lms_update()` 和 `lms_process()`。由于 LMS 包含点积累加、误差信号抵消、步长敏感性和长期权重递推更新，该程序非常适合测试混合精度工具是否能够识别关键状态变量并在误差约束下安全降低非关键变量的精度。