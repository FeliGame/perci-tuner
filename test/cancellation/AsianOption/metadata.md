# asian_option_mc_target.c 程序说明

## 1. 程序简介

`asian_option_mc_target.c` 是一个面向混合精度研究的亚式期权 Monte Carlo 定价程序。程序基于几何布朗运动模拟标的资产价格路径，并计算算术平均价格亚式看涨期权的折现期望收益。

亚式期权的 payoff 依赖整个价格路径的平均值，而不是仅依赖到期价格。因此，该程序同时包含随机路径模拟、指数函数更新、路径内累加、payoff 截断和跨路径求和等典型数值结构，非常适合作为金融 Monte Carlo 类混合精度 benchmark。

核心数学模型为：

```text
dS = r*S*dt + sigma*S*dW
```

离散化后使用：

```text
S_{t+1} = S_t * exp((r - 0.5*sigma^2)*dt + sigma*sqrt(dt)*Z_t)
```

其中 `Z_t` 是标准正态随机变量。

算术平均亚式看涨期权 payoff 为：

```text
payoff = max(avg(S_1, S_2, ..., S_N) - K, 0)
```

期权价格为：

```text
price = exp(-r*T) * mean(payoff)
```

## 2. 应用领域

该程序属于以下应用领域：

```text
金融工程
衍生品定价
Monte Carlo 模拟
随机微分方程路径模拟
几何布朗运动
混合精度 benchmark
```

从 benchmark 分类角度看，该程序可以归为：

```text
Monte Carlo finance benchmark
stochastic path simulation
Asian option pricing
GBM simulation workload
randomized numerical integration
```

该程序适合测试混合精度工具在随机模拟、指数函数调用、路径递推状态、长累加和 payoff 分支截断等场景下的有效性。

## 3. 核心数学结构

程序模拟的是几何布朗运动：

```text
dS = r*S*dt + sigma*S*dW
```

对应离散路径更新为：

```text
S = S * exp(drift + vol_sqrt_dt * Z)
```

其中：

```text
dt = T / N_steps
drift = (r - 0.5*sigma^2) * dt
vol_sqrt_dt = sigma * sqrt(dt)
Z ~ N(0, 1)
```

路径平均值为：

```text
avg = (S_1 + S_2 + ... + S_N) / N_steps
```

payoff 为：

```text
payoff = max(avg - K, 0)
```

最终期权价格为：

```text
price = exp(-r*T) * payoff_sum / N_paths
```

## 4. 程序定位

该程序是一个“目标程序版本”，适合作为混合精度工具链输入。它不包含高精度 reference，也不包含完整逐路径误差指标。程序自身主要负责：

```text
1. 初始化 Monte Carlo 参数；
2. 使用 RNG 生成均匀随机数；
3. 使用 Box-Muller 生成标准正态随机数；
4. 模拟几何布朗运动价格路径；
5. 计算亚式期权 payoff；
6. 对 payoff 进行跨路径累加；
7. 输出期权价格、采样标准误差、checksum 和运行时间。
```

正式数值误差验证应由外部 harness 完成，例如固定 seed 后比较 baseline 与 mixed-precision 版本的 price、payoff 序列或最终统计量差异。

## 5. 程序主要流程

程序整体流程如下：

```text
1. 解析命令行参数；
2. 设置 S0、K、r、sigma、T、N_steps、N_paths；
3. 计算 dt、discount、drift、vol_sqrt_dt；
4. 初始化随机数生成器；
5. 对每条 Monte Carlo 路径：
   1. 初始化 S = S0；
   2. 循环 N_steps 次；
   3. 生成标准正态随机数 Z；
   4. 更新 S；
   5. 累加路径平均值 avg_sum；
   6. 计算 avg；
   7. 计算 payoff = max(avg - K, 0)；
   8. 累加 payoff；
6. 计算折现期权价格；
7. 输出价格、方差、标准误差、checksum 和时间。
```

主计算可以概括为：

```text
for p in paths:
    S = S0
    avg_sum = 0
    for t in steps:
        Z = gaussian_random()
        S *= exp(drift + vol_sqrt_dt * Z)
        avg_sum += S
    avg = avg_sum / N_steps
    payoff = max(avg - K, 0)
    payoff_sum += payoff

price = exp(-r*T) * payoff_sum / N_paths
```

## 6. 核心函数说明

### 6.1 `rng_uniform01()`

```c
double rng_uniform01(RNGState *rng)
```

该函数使用 `splitmix64` 生成 `(0,1)` 区间内的均匀随机数。它避免返回 0，因为 Box-Muller 变换中需要计算 `log(u1)`。

该函数对应随机数质量和随机数精度敏感点。

### 6.2 `gaussian_random()`

```c
double gaussian_random(RNGState *rng)
```

该函数使用 Box-Muller 方法生成标准正态随机数。

核心计算包括：

```text
radius = sqrt(-2 * log(u1))
theta  = 2*pi*u2
z0 = radius * cos(theta)
z1 = radius * sin(theta)
```

该函数包含 `log()`、`sqrt()`、`sin()` 和 `cos()` 等数学函数调用，是函数级混合精度搜索的重要候选区域。

### 6.3 `simulate_path_payoff()`

```c
double simulate_path_payoff(double S0,
                            double K,
                            double drift,
                            double vol_sqrt_dt,
                            int N_steps,
                            RNGState *rng)
```

该函数模拟一条资产价格路径，并返回该路径对应的算术平均亚式看涨期权 payoff。

核心计算为：

```c
double Z = gaussian_random(rng);
double exponent_arg = drift + vol_sqrt_dt * Z;
double growth = exp(exponent_arg);

S *= growth;
avg_sum += S;
```

该函数是路径级核心计算区域。

### 6.4 `price_asian_MC()`

```c
AsianMCResult price_asian_MC(double S0,
                             double K,
                             double r,
                             double sigma,
                             double T,
                             int N_steps,
                             int N_paths,
                             uint64_t seed)
```

该函数执行完整 Monte Carlo 定价，负责多路径模拟、payoff 求和、方差估计、标准误差估计和最终价格计算。

输出结构包括：

```text
price
mean_payoff
stderr_price
payoff_variance
payoff_sum
payoff_sq_sum
checksum_payoff
```

### 6.5 `main()`

`main()` 负责参数解析、重复运行计时、调用 `price_asian_MC()` 并打印结果。

## 7. 编译方式

推荐编译命令：

```bash
gcc -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    asian_option_mc_target.c -lm -o asian_mc
```

或使用 Clang：

```bash
clang -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    asian_option_mc_target.c -lm -o asian_mc
```

参数说明：

| 参数                | 含义               |
| ------------------- | ------------------ |
| `-std=c99`          | 使用 C99 标准      |
| `-O3`               | 开启优化           |
| `-fno-fast-math`    | 禁止不安全浮点优化 |
| `-ffp-contract=off` | 禁止自动 FMA 融合  |
| `-lm`               | 链接数学库         |

对于精度研究，不建议使用：

```bash
-ffast-math
```

因为它可能改变浮点表达式重排、数学函数近似、NaN/Inf 语义和乘加融合策略，从而干扰混合精度搜索结果。

## 8. 运行方式

程序运行格式为：

```bash
./asian_mc [S0] [K] [r] [sigma] [T] [N_steps] [N_paths] [iters] [seed]
```

默认参数为：

```text
S0 = 100
K = 100
r = 0.05
sigma = 0.20
T = 1.0
N_steps = 252
N_paths = 200000
iters = 3
seed = 1
```

示例：

```bash
./asian_mc 100 100 0.05 0.20 1.0 252 200000 3 1
```

高波动率测试：

```bash
./asian_mc 100 100 0.05 0.80 1.0 252 500000 3 1
```

短路径快速测试：

```bash
./asian_mc 100 100 0.05 0.20 1.0 32 10000 1 1
```

## 9. 参数说明

| 参数      | 含义                       |
| --------- | -------------------------- |
| `S0`      | 初始资产价格               |
| `K`       | 执行价格                   |
| `r`       | 无风险利率                 |
| `sigma`   | 波动率                     |
| `T`       | 到期时间                   |
| `N_steps` | 每条路径的离散时间步数     |
| `N_paths` | Monte Carlo 路径数量       |
| `iters`   | 重复运行次数，用于稳定计时 |
| `seed`    | 随机数种子                 |

其中，`N_steps` 控制单条路径的时间离散精度，`N_paths` 控制 Monte Carlo 采样误差。`sigma` 越大，路径波动越强，payoff 方差也通常越大。

## 10. 输出说明

程序输出如下字段：

```text
benchmark
S0
K
r
sigma
T
N_steps
N_paths
iters
seed
precision
sizeof_double
DBL_MANT_DIG
DBL_DIG
avg_kernel_time_sec
gops_est
price
mean_payoff
stderr_price
payoff_variance
payoff_sum
payoff_sq_sum
checksum_payoff
```

主要字段含义如下：

| 输出项                | 含义                     |
| --------------------- | ------------------------ |
| `price`               | 折现后的亚式期权价格     |
| `mean_payoff`         | 平均 payoff              |
| `stderr_price`        | Monte Carlo 采样标准误差 |
| `payoff_variance`     | payoff 样本方差          |
| `payoff_sum`          | payoff 总和              |
| `payoff_sq_sum`       | payoff 平方和            |
| `checksum_payoff`     | 加权 payoff checksum     |
| `avg_kernel_time_sec` | 平均运行时间             |
| `gops_est`            | 估算吞吐量               |

需要注意，`stderr_price` 反映的是 Monte Carlo 采样误差，不是数值舍入误差。混合精度误差需要通过 baseline 与转换后版本之间的对比来评估。

## 11. 核心热点函数

程序的主要热点函数包括：

| 函数                     | 作用                          | 热点原因                                |
| ------------------------ | ----------------------------- | --------------------------------------- |
| `price_asian_MC()`       | 多路径 Monte Carlo 定价       | 外层路径循环，调用次数由 `N_paths` 决定 |
| `simulate_path_payoff()` | 单路径 GBM 模拟和 payoff 计算 | 内层时间步循环，包含 `exp()` 和累加     |
| `gaussian_random()`      | 标准正态随机数生成            | 每个时间步需要一个高斯随机数            |
| `rng_uniform01()`        | 均匀随机数生成                | Box-Muller 的基础随机数来源             |

运行时间主要集中在：

```text
price_asian_MC()
  -> simulate_path_payoff()
      -> gaussian_random()
      -> exp()
```

其中 `exp()`、`log()`、`sqrt()`、`sin()` 和 `cos()` 是重要的数学函数调用热点。

## 12. 数值敏感点

该程序包含多个典型混合精度敏感点。

### 12.1 RNG 精度与质量

随机数会影响每条价格路径。如果混合精度转换影响随机数生成或正态随机数变换，路径分布可能改变。

### 12.2 `exp()` 路径更新

价格更新为：

```text
S *= exp(drift + vol_sqrt_dt * Z)
```

指数函数的输入误差可能被 `exp()` 放大。由于 `S` 是递推状态，单步误差会影响后续所有时间步。

### 12.3 路径内累加

亚式期权使用路径平均：

```text
avg_sum += S
```

当 `N_steps` 较大时，路径内累加误差会增加。

### 12.4 payoff 小数消去

payoff 为：

```text
max(avg - K, 0)
```

当 `avg` 接近 `K` 时，`avg - K` 会发生 cancellation。微小精度误差可能改变 payoff 是否为正。

### 12.5 跨路径求和

最终定价需要：

```text
payoff_sum += payoff
```

当 `N_paths` 很大时，payoff 求和误差会逐渐积累。

### 12.6 方差估计

程序计算：

```text
variance = mean(payoff^2) - mean(payoff)^2
```

这是典型的差值表达式。如果两项接近，可能出现消去误差。

## 13. 混合精度搜索对象

该程序适合变量级和函数级混合精度工具搜索。重点对象包括：

| 对象            | 说明             | 敏感性           |
| --------------- | ---------------- | ---------------- |
| `S`             | 路径资产价格状态 | 极高             |
| `Z`             | 标准正态随机数   | 高               |
| `exponent_arg`  | `exp()` 输入     | 极高             |
| `growth`        | 指数增长因子     | 高               |
| `avg_sum`       | 路径内价格累加   | 高               |
| `avg`           | 路径平均价格     | 高               |
| `intrinsic`     | `avg - K`        | 高，尤其接近 ATM |
| `payoff`        | 单路径收益       | 中到高           |
| `payoff_sum`    | 跨路径收益累加   | 高               |
| `payoff_sq_sum` | 方差估计累加     | 高               |
| `discount`      | 折现因子         | 中               |
| `drift`         | 漂移项           | 高               |
| `vol_sqrt_dt`   | 波动项系数       | 高               |

其中，`S`、`exponent_arg`、`avg_sum`、`intrinsic` 和 `payoff_sum` 是最重要的精度敏感变量。

## 14. 推荐测试配置

标准测试：

```bash
./asian_mc 100 100 0.05 0.20 1.0 252 200000 3 1
```

高波动率压力测试：

```bash
./asian_mc 100 100 0.05 0.80 1.0 252 500000 3 1
```

深度价内测试：

```bash
./asian_mc 100 80 0.05 0.20 1.0 252 200000 3 1
```

平值附近 cancellation 测试：

```bash
./asian_mc 100 100 0.05 0.20 1.0 252 500000 3 1
```

长时间步路径累加测试：

```bash
./asian_mc 100 100 0.05 0.20 1.0 1000 200000 3 1
```

大路径数 payoff 累加测试：

```bash
./asian_mc 100 100 0.05 0.20 1.0 252 1000000 3 1
```

## 15. 外部误差验证建议

由于本程序不内置高精度 reference，建议外部 harness 按如下流程验证混合精度配置：

```text
1. 编译并运行原始 double baseline；
2. 固定 seed、N_steps 和 N_paths；
3. 保存 baseline 的 price、mean_payoff、payoff_sum 和 checksum_payoff；
4. 编译并运行 mixed-precision 转换版本；
5. 使用相同输入参数和 seed；
6. 比较价格、payoff 统计量和 checksum；
7. 判断误差是否满足阈值；
8. 比较运行时间判断是否加速。
```

推荐误差指标包括：

```text
abs_price_error
rel_price_error
payoff_sum_error
checksum_payoff_error
stderr-aware_error_ratio
```

由于 Monte Carlo 本身存在采样误差，建议固定随机数种子后比较 deterministic 输出。否则不同随机路径会让数值误差和采样误差混在一起。

## 16. 作为 benchmark 的分类建议

该程序可以归类为：

```text
Monte Carlo finance benchmark
Asian option pricing
stochastic path simulation
GBM path recurrence
payoff accumulation benchmark
```

在混合精度 benchmark 中，可以归入以下危险类型：

| 分类         | 说明                                       |
| ------------ | ------------------------------------------ |
| 随机采样     | RNG 和 Gaussian transform 影响路径分布     |
| 指数递推     | `S *= exp(...)` 误差会沿路径传播           |
| 路径内累加   | `avg_sum += S` 长时间步累加                |
| payoff 分支  | `fmax(avg-K,0)` 对 ATM 情况敏感            |
| 跨路径归约   | `payoff_sum` 和 `payoff_sq_sum` 大规模累加 |
| 方差消去     | `E[X^2] - E[X]^2` 可能有 cancellation      |
| 函数调用敏感 | `exp/log/sqrt/sin/cos` 精度影响明显        |

## 17. 注意事项

1. `stderr_price` 是 Monte Carlo 采样误差，不是混合精度误差。
2. 比较混合精度结果时必须固定 seed。
3. 不建议只用较少路径数判断精度，因为采样噪声较大。
4. ATM 情况下 `avg` 接近 `K`，payoff 对误差更敏感。
5. 高波动率会增大 payoff 方差，也会放大部分数值差异。
6. `price_asian_MC()` 中 payoff 求和使用 `long double` 累加，以降低输出统计量自身的求和误差。
7. 如果工具会搜索数学函数调用，需要谨慎处理 `exp()`、`log()`、`sqrt()`、`sin()` 和 `cos()`。
8. 不建议开启 `-ffast-math`。

## 18. 总结

`asian_option_mc_target.c` 是一个面向混合精度研究的金融 Monte Carlo benchmark。它通过几何布朗运动模拟资产价格路径，并计算算术平均亚式看涨期权价格。程序核心热点包括 `price_asian_MC()`、`simulate_path_payoff()`、`gaussian_random()` 和 `rng_uniform01()`。由于包含随机数生成、指数路径递推、路径平均累加、payoff 分支截断和跨路径求和，该程序非常适合测试混合精度工具在随机模拟和金融数值计算场景中的有效性。