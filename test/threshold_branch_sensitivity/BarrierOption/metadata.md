# barrier_option_mc_target.c 程序说明

## 1. 程序简介

`barrier_option_mc_target.c` 是一个面向混合精度研究的障碍期权 Monte Carlo 定价程序。程序基于几何布朗运动模拟资产价格路径，并根据路径是否触碰给定障碍价格来决定期权是否生效或失效。

障碍期权属于典型路径依赖期权。与普通欧式期权只依赖到期价格不同，障碍期权还依赖整个路径过程中是否曾经穿越障碍水平。因此，该程序包含随机路径模拟、指数递推、路径极值跟踪、障碍触发分支、payoff 分支和跨路径归约等多个混合精度敏感结构。

核心价格路径模型为：

```text
dS = r*S*dt + sigma*S*dW
```

离散更新形式为：

```text
S_{t+1} = S_t * exp((r - 0.5*sigma^2)*dt + sigma*sqrt(dt)*Z_t)
```

其中 `Z_t` 是标准正态随机变量。

## 2. 应用领域

该程序属于以下应用领域：

```text
金融工程
衍生品定价
障碍期权定价
Monte Carlo 模拟
随机微分方程路径模拟
路径依赖金融计算
混合精度 benchmark
```

从 benchmark 分类角度看，该程序可以归类为：

```text
Monte Carlo finance benchmark
barrier option pricing
path-dependent stochastic simulation
branch-sensitive payoff benchmark
GBM path recurrence workload
```

该程序特别适合测试混合精度工具在路径依赖分支、障碍条件判断和随机递推状态中的有效性。

## 3. 支持的期权类型

程序支持 4 种障碍类型：

| `barrier_mode` | 类型         | 含义                       |
| -------------- | ------------ | -------------------------- |
| `0`            | up-and-out   | 价格向上触碰障碍后期权失效 |
| `1`            | down-and-out | 价格向下触碰障碍后期权失效 |
| `2`            | up-and-in    | 价格向上触碰障碍后期权生效 |
| `3`            | down-and-in  | 价格向下触碰障碍后期权生效 |

程序还支持两种 payoff 类型：

| `option_type` | 类型 | payoff            |
| ------------- | ---- | ----------------- |
| `0`           | call | `max(S_T - K, 0)` |
| `1`           | put  | `max(K - S_T, 0)` |

因此，该程序可以覆盖 up-and-out call、down-and-out put、up-and-in call、down-and-in put 等典型障碍期权场景。

## 4. 核心数学结构

价格路径按几何布朗运动离散模拟：

```text
S_new = S * exp(drift + vol_sqrt_dt * Z)
```

其中：

```text
dt = T / N_steps
drift = (r - 0.5*sigma^2) * dt
vol_sqrt_dt = sigma * sqrt(dt)
```

障碍判断逻辑为：

```text
up barrier:   crossed = S >= B
down barrier: crossed = S <= B
```

对于 knock-out 类期权：

```text
active = !crossed
```

对于 knock-in 类期权：

```text
active = crossed
```

最终 payoff 为：

```text
payoff = active ? vanilla_payoff : 0
```

其中 vanilla payoff 为：

```text
call: max(S_T - K, 0)
put:  max(K - S_T, 0)
```

## 5. 程序定位

该程序是一个“目标程序版本”，适合作为混合精度工具链输入。程序本身不包含高精度 reference，也不包含完整逐路径误差指标。它主要负责：

```text
1. 解析障碍期权参数；
2. 使用随机数生成器生成标准正态随机数；
3. 模拟资产价格路径；
4. 记录路径最小值和最大值；
5. 判断路径是否触碰障碍；
6. 根据 in/out 类型决定 payoff 是否生效；
7. 对多条路径进行 Monte Carlo 平均；
8. 输出价格、命中比例、路径统计量、checksum 和运行时间。
```

正式误差验证应由外部 harness 完成，例如固定随机种子后比较 baseline 和 mixed-precision 版本的价格、`crossed_count`、`hit_ratio`、`checksum_payoff` 和路径统计量。

## 6. 程序主要流程

程序整体流程如下：

```text
1. 解析命令行参数；
2. 设置 S0、K、B、r、sigma、T、N_steps、N_paths；
3. 计算 dt、drift、vol_sqrt_dt；
4. 初始化随机数生成器；
5. 对每条 Monte Carlo 路径：
   1. 初始化 S = S0；
   2. 初始化 path_min = S0；
   3. 初始化 path_max = S0；
   4. 判断初始点是否已经触碰障碍；
   5. 对每个时间步：
      1. 生成标准正态随机数 Z；
      2. 更新资产价格 S；
      3. 更新 path_min 和 path_max；
      4. 判断是否触碰障碍；
   6. 计算普通欧式 payoff；
   7. 根据 barrier_mode 判断 payoff 是否生效；
   8. 累加 payoff 和路径统计量；
6. 计算折现期权价格；
7. 输出统计结果。
```

简化伪代码为：

```text
for each path:
    S = S0
    crossed = barrier_is_crossed(S0, B)

    for each time step:
        Z = gaussian_random()
        S = S * exp(drift + vol_sqrt_dt * Z)

        path_min = min(path_min, S)
        path_max = max(path_max, S)

        if barrier_is_crossed(S, B):
            crossed = true

    vanilla = payoff(S, K)

    if knock-out:
        payoff = crossed ? 0 : vanilla
    if knock-in:
        payoff = crossed ? vanilla : 0

    payoff_sum += payoff

price = exp(-r*T) * payoff_sum / N_paths
```

## 7. 核心函数说明

### 7.1 `rng_uniform01()`

```c
double rng_uniform01(RNGState *rng)
```

该函数基于 `splitmix64` 生成 `(0,1)` 区间内的均匀随机数。它避免返回 0，因为 Box-Muller 变换中需要计算 `log(u1)`。

该函数对应随机数质量和随机数精度敏感点。

### 7.2 `gaussian_random()`

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

该函数包含 `log()`、`sqrt()`、`sin()`、`cos()` 等数学函数调用，是函数级混合精度搜索的重要候选区域。

### 7.3 `barrier_is_crossed()`

```c
int barrier_is_crossed(double S, double B, int barrier_mode)
```

该函数判断当前价格是否触碰障碍。

判断逻辑为：

```text
up barrier:   S >= B
down barrier: S <= B
```

这是障碍期权中最关键的分支敏感区域。若混合精度转换导致 `S` 在障碍附近发生微小变化，就可能改变 `crossed` 状态，从而使 payoff 从正值变为 0，或从 0 变为正值。

### 7.4 `european_payoff()`

```c
double european_payoff(double S, double K, int option_type)
```

该函数计算普通欧式 call 或 put payoff：

```text
call = max(S - K, 0)
put  = max(K - S, 0)
```

当 `S` 接近 `K` 时，`S-K` 或 `K-S` 存在 cancellation 风险，且 payoff 分支对微小误差敏感。

### 7.5 `simulate_barrier_payoff()`

```c
BarrierPathResult simulate_barrier_payoff(...)
```

该函数模拟单条障碍期权路径并返回路径结果。

它同时负责：

```text
1. 生成标准正态随机数；
2. 更新资产价格 S；
3. 跟踪 path_min 和 path_max；
4. 判断障碍是否触碰；
5. 计算最终 payoff；
6. 返回 final_S、path_min、path_max 和 crossed。
```

这是程序最核心的路径级计算函数。

### 7.6 `price_barrier_MC()`

```c
BarrierMCResult price_barrier_MC(...)
```

该函数执行完整 Monte Carlo 定价。它对所有路径的 payoff 进行累加，并计算价格、方差、标准误差、障碍触碰比例、终端价格均值和路径极值均值。

输出结构包括：

```text
price
mean_payoff
stderr_price
payoff_variance
hit_ratio
mean_final_S
mean_path_min
mean_path_max
crossed_count
payoff_sum
payoff_sq_sum
checksum_payoff
```

## 8. 编译方式

推荐编译命令：

```bash
gcc -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    barrier_option_mc_target.c -lm -o barrier_mc
```

或使用 Clang：

```bash
clang -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    barrier_option_mc_target.c -lm -o barrier_mc
```

参数说明：

| 参数                | 含义               |
| ------------------- | ------------------ |
| `-std=c99`          | 使用 C99 标准      |
| `-O3`               | 开启优化           |
| `-fno-fast-math`    | 禁止不安全浮点优化 |
| `-ffp-contract=off` | 禁止自动 FMA 融合  |
| `-lm`               | 链接数学库         |

不建议使用：

```bash
-ffast-math
```

因为它可能改变浮点表达式重排、数学函数近似、NaN/Inf 语义和分支行为，从而干扰混合精度实验。

## 9. 运行方式

程序运行格式为：

```bash
./barrier_mc [S0] [K] [B] [r] [sigma] [T] [N_steps] [N_paths] [iters] [barrier_mode] [option_type] [seed]
```

默认参数为：

```text
S0 = 100
K = 100
B = 120
r = 0.05
sigma = 0.20
T = 1.0
N_steps = 252
N_paths = 200000
iters = 3
barrier_mode = 0
option_type = 0
seed = 1
```

标准 up-and-out call 测试：

```bash
./barrier_mc 100 100 120 0.05 0.20 1.0 252 200000 3 0 0 1
```

down-and-out put 测试：

```bash
./barrier_mc 100 100 80 0.05 0.20 1.0 252 200000 3 1 1 1
```

up-and-in call 测试：

```bash
./barrier_mc 100 100 120 0.05 0.20 1.0 252 200000 3 2 0 1
```

down-and-in put 测试：

```bash
./barrier_mc 100 100 80 0.05 0.20 1.0 252 200000 3 3 1 1
```

## 10. 参数说明

| 参数           | 含义                       |
| -------------- | -------------------------- |
| `S0`           | 初始资产价格               |
| `K`            | 执行价格                   |
| `B`            | 障碍价格                   |
| `r`            | 无风险利率                 |
| `sigma`        | 波动率                     |
| `T`            | 到期时间                   |
| `N_steps`      | 每条路径的离散时间步数     |
| `N_paths`      | Monte Carlo 路径数量       |
| `iters`        | 重复运行次数，用于稳定计时 |
| `barrier_mode` | 障碍类型                   |
| `option_type`  | call 或 put                |
| `seed`         | 随机数种子                 |

## 11. 输出说明

程序输出如下字段：

```text
benchmark
S0
K
B
r
sigma
T
N_steps
N_paths
iters
barrier_mode
option_type
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
hit_ratio
crossed_count
mean_final_S
mean_path_min
mean_path_max
payoff_sum
payoff_sq_sum
final_S_sum
path_min_sum
path_max_sum
checksum_payoff
```

主要字段含义如下：

| 输出项            | 含义                     |
| ----------------- | ------------------------ |
| `price`           | 折现后的障碍期权价格     |
| `mean_payoff`     | 平均 payoff              |
| `stderr_price`    | Monte Carlo 采样标准误差 |
| `payoff_variance` | payoff 样本方差          |
| `hit_ratio`       | 触碰障碍的路径比例       |
| `crossed_count`   | 触碰障碍的路径数量       |
| `mean_final_S`    | 终端资产价格均值         |
| `mean_path_min`   | 路径最小值均值           |
| `mean_path_max`   | 路径最大值均值           |
| `payoff_sum`      | payoff 总和              |
| `payoff_sq_sum`   | payoff 平方和            |
| `checksum_payoff` | 加权 payoff checksum     |

其中 `hit_ratio` 和 `crossed_count` 对障碍期权非常关键。混合精度转换可能改变边界附近路径是否触碰障碍，从而显著影响价格。

## 12. 核心热点函数

程序的主要热点函数包括：

| 函数                        | 作用                    | 热点原因                  |
| --------------------------- | ----------------------- | ------------------------- |
| `price_barrier_MC()`        | 多路径 Monte Carlo 定价 | 外层路径循环，路径数很大  |
| `simulate_barrier_payoff()` | 单路径模拟和障碍判断    | 内层时间步循环            |
| `barrier_is_crossed()`      | 判断是否触碰障碍        | 分支敏感，调用频繁        |
| `gaussian_random()`         | 标准正态随机数生成      | 每个时间步调用            |
| `rng_uniform01()`           | 均匀随机数生成          | Box-Muller 基础随机数来源 |

运行时间主要集中在：

```text
price_barrier_MC()
  -> simulate_barrier_payoff()
      -> gaussian_random()
      -> exp()
      -> barrier_is_crossed()
```

## 13. 数值敏感点

该程序包含多个典型混合精度敏感点。

### 13.1 障碍触碰分支

核心判断为：

```text
up barrier:   S >= B
down barrier: S <= B
```

当路径价格 `S` 非常接近障碍 `B` 时，微小舍入误差可能改变 `crossed` 的值。一旦 `crossed` 改变，knock-in 或 knock-out 期权的 payoff 可能完全不同。

### 13.2 指数路径更新

资产路径更新为：

```text
S *= exp(drift + vol_sqrt_dt * Z)
```

`exp()` 输入误差会影响增长因子，而 `S` 是递推状态，误差会沿时间步传播。

### 13.3 路径极值统计

程序跟踪：

```text
path_min
path_max
```

这些变量与障碍触碰密切相关。低精度可能改变路径极值，从而改变障碍判断结果。

### 13.4 payoff 分支

普通 payoff 为：

```text
call = max(S - K, 0)
put  = max(K - S, 0)
```

当 `S` 接近 `K` 时，payoff 分支也对精度敏感。

### 13.5 Monte Carlo 归约

最终价格依赖大量路径 payoff 的累加：

```text
payoff_sum += payoff
```

当 `N_paths` 很大时，归约误差会逐渐积累。

## 14. 混合精度搜索对象

该程序适合变量级和函数级混合精度工具搜索。重点对象包括：

| 对象           | 说明               | 敏感性 |
| -------------- | ------------------ | ------ |
| `S`            | 资产价格递推状态   | 极高   |
| `Z`            | 标准正态随机数     | 高     |
| `exponent_arg` | `exp()` 输入       | 极高   |
| `growth`       | 指数增长因子       | 高     |
| `path_min`     | 路径最小值         | 高     |
| `path_max`     | 路径最大值         | 高     |
| `crossed`      | 障碍触碰状态       | 极高   |
| `vanilla`      | 普通欧式 payoff    | 中到高 |
| `payoff`       | 障碍期权 payoff    | 高     |
| `payoff_sum`   | 跨路径 payoff 总和 | 高     |
| `hit_ratio`    | 障碍触碰比例       | 高     |

最重要的变量通常是 `S`、`exponent_arg`、`path_min`、`path_max`、`crossed` 和 `payoff_sum`。

## 15. 推荐测试配置

标准 up-and-out call：

```bash
./barrier_mc 100 100 120 0.05 0.20 1.0 252 200000 3 0 0 1
```

障碍更接近初始价格的敏感测试：

```bash
./barrier_mc 100 100 105 0.05 0.20 1.0 252 200000 3 0 0 1
```

down-and-out put：

```bash
./barrier_mc 100 100 80 0.05 0.20 1.0 252 200000 3 1 1 1
```

up-and-in call：

```bash
./barrier_mc 100 100 120 0.05 0.20 1.0 252 200000 3 2 0 1
```

高波动率压力测试：

```bash
./barrier_mc 100 100 120 0.05 0.80 1.0 252 500000 3 0 0 1
```

长路径时间步测试：

```bash
./barrier_mc 100 100 120 0.05 0.20 1.0 1000 200000 3 0 0 1
```

## 16. 外部误差验证建议

由于本程序不内置高精度 reference，建议外部 harness 按如下流程验证混合精度配置：

```text
1. 编译并运行原始 double baseline；
2. 固定 seed、N_steps、N_paths、barrier_mode 和 option_type；
3. 保存 baseline 的 price、crossed_count、hit_ratio、checksum_payoff；
4. 编译并运行 mixed-precision 转换版本；
5. 使用完全相同的输入参数和 seed；
6. 比较价格、障碍触碰统计、路径极值和 checksum；
7. 检查误差是否满足阈值；
8. 比较运行时间判断是否加速。
```

推荐误差指标包括：

```text
abs_price_error
rel_price_error
payoff_sum_error
checksum_payoff_error
hit_ratio_error
crossed_count_diff
mean_path_min_error
mean_path_max_error
stderr-aware_error_ratio
```

对于障碍期权，`crossed_count_diff` 非常重要。即使价格误差暂时较小，触碰障碍路径数量发生明显变化也说明路径分支行为已经被改变。

## 17. 作为 benchmark 的分类建议

该程序可以归类为：

```text
Monte Carlo finance benchmark
barrier option pricing
path-dependent stochastic simulation
branch-sensitive payoff benchmark
GBM path recurrence workload
```

在混合精度 benchmark 中，可以归入以下危险类型：

| 分类         | 说明                                      |
| ------------ | ----------------------------------------- |
| 随机采样     | 正态随机数影响路径分布                    |
| 递推状态     | `S` 跨时间步递推更新                      |
| 指数路径更新 | `S *= exp(...)` 误差沿路径传播            |
| 障碍分支敏感 | `S >= B` 或 `S <= B` 改变 payoff 是否生效 |
| 路径极值敏感 | `path_min/path_max` 决定触碰行为          |
| payoff 分支  | `max(S-K,0)` 或 `max(K-S,0)`              |
| 大规模归约   | payoff 跨路径求和                         |
| 路径依赖     | payoff 依赖整个路径而非只依赖终点         |

## 18. 注意事项

1. `stderr_price` 是 Monte Carlo 采样误差，不是混合精度数值误差。
2. 比较 mixed-precision 版本时必须固定随机数种子。
3. `barrier_mode` 和 `option_type` 必须固定，否则不同产品本身价格不同。
4. 障碍价格越接近初始价格或常见路径区域，程序越分支敏感。
5. 高波动率会增加触碰障碍的概率，也会增强路径分支敏感性。
6. 不建议只比较 price，也应比较 `crossed_count`、`hit_ratio`、`mean_path_min` 和 `mean_path_max`。
7. 如果工具搜索数学函数调用，应谨慎处理 `exp()`、`log()`、`sqrt()`、`sin()` 和 `cos()`。
8. 不建议开启 `-ffast-math`。

## 19. 总结

`barrier_option_mc_target.c` 是一个面向混合精度研究的障碍期权 Monte Carlo benchmark。它使用几何布朗运动模拟资产价格路径，并根据路径是否触碰障碍决定 payoff 是否生效。该程序的核心热点包括 `price_barrier_MC()`、`simulate_barrier_payoff()`、`barrier_is_crossed()`、`gaussian_random()` 和 `rng_uniform01()`。由于包含随机路径递推、指数函数更新、路径极值跟踪、障碍触碰分支和 payoff 大规模归约，该程序非常适合测试混合精度工具在路径依赖金融计算场景中的有效性。