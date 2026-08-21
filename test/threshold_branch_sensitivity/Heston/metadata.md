# heston_mc_target.c 程序说明

## 1. 程序简介

`heston_mc_target.c` 是一个面向混合精度研究的 Heston 随机波动率模型 Monte Carlo 定价程序。程序模拟 Heston 模型下的资产价格路径和随机方差路径，并计算欧式看涨期权价格。

Heston 模型相比 Black-Scholes 模型更复杂，因为它不再假设波动率为常数，而是将方差 `V` 本身建模为随机过程：

```text
dS = r*S*dt + sqrt(V)*S*dW_S

dV = kappa*(theta - V)*dt + xi*sqrt(V)*dW_V
```

其中 `S` 是资产价格，`V` 是瞬时方差，`r` 是无风险利率，`kappa` 是方差均值回复速度，`theta` 是长期方差均值，`xi` 是波动率的波动率，`rho` 是资产价格随机扰动和方差随机扰动之间的相关系数。

该程序特别适合作为混合精度 benchmark，因为它包含随机数生成、相关高斯变量、指数路径更新、随机方差递推、平方根近零敏感性、负方差修复分支、payoff 分支以及跨路径归约等多个典型精度敏感结构。

## 2. 应用领域

该程序属于以下应用领域：

```text
金融工程
随机波动率模型
期权定价
Monte Carlo 模拟
随机微分方程路径模拟
混合精度 benchmark
```

从 benchmark 分类角度看，可以归为：

```text
Monte Carlo finance benchmark
Heston stochastic volatility model
stochastic differential equation simulation
state-recursive financial simulation
branch-sensitive variance process benchmark
```

它适合测试混合精度工具在随机模拟、递推状态变量、数学函数调用和分支敏感场景下的有效性。

## 3. 核心数学模型

Heston 模型包含两个耦合随机过程。

资产价格过程：

```text
dS = r*S*dt + sqrt(V)*S*dW_S
```

方差过程：

```text
dV = kappa*(theta - V)*dt + xi*sqrt(V)*dW_V
```

其中：

| 参数      | 含义                                 |
| --------- | ------------------------------------ |
| `S0`      | 初始资产价格                         |
| `K`       | 执行价格                             |
| `r`       | 无风险利率                           |
| `V0`      | 初始方差                             |
| `kappa`   | 方差均值回复速度                     |
| `theta`   | 方差长期均值                         |
| `xi`      | 方差波动强度，即 vol-of-vol          |
| `rho`     | 资产价格噪声和方差噪声之间的相关系数 |
| `T`       | 到期时间                             |
| `N_steps` | 时间离散步数                         |
| `N_paths` | Monte Carlo 路径数                   |

离散化后，资产价格更新为：

```text
S_new = S * exp((r - 0.5*max(V,0))*dt + sqrt(max(V,0))*sqrt(dt)*Z_S)
```

方差更新为：

```text
V_new = V + kappa*(theta - V)*dt
          + xi*sqrt(max(V,0))*sqrt(dt)*Z_V
```

由于 Euler 离散下 `V_new` 可能变为负数，程序提供多种负方差修复方案。

## 4. 负方差处理方案

Heston 模型的一个经典数值问题是：理论上 CIR 方差过程应保持非负，但离散化模拟时，`V_new` 可能变成负数。程序支持三种处理方案：

| scheme | 名称              | 处理方式                                       |
| ------ | ----------------- | ---------------------------------------------- |
| `0`    | truncation 截断法 | `V_new = fmax(V_proposal, 0.0)`                |
| `1`    | reflection 反射法 | `V_new = fabs(V_proposal)`                     |
| `2`    | full truncation   | drift 和 diffusion 使用 `max(V,0)`，最后再截断 |

对应程序中的核心逻辑为：

```c
double V_pos = fmax(V, 0.0);
double sqrt_V = sqrt(V_pos);

double drift;
if (scheme == 2) {
    drift = kappa * (theta - V_pos) * dt;
} else {
    drift = kappa * (theta - V) * dt;
}

double diffusion = xi * sqrt_V * sqrt_dt * Z_V;
double V_proposal = V + drift + diffusion;

if (scheme == 0 || scheme == 2) {
    V_new = fmax(V_proposal, 0.0);
} else if (scheme == 1) {
    V_new = fabs(V_proposal);
}
```

不同方案具有不同偏差特性，因此在混合精度测试中，应固定 `scheme` 后再比较不同精度配置。

## 5. 程序定位

该程序是一个“目标程序版本”，适合作为混合精度工具链输入。程序本身不包含高精度 reference，也不进行完整误差指标计算。它主要负责：

```text
1. 解析 Heston 模型参数；
2. 使用 splitmix64 生成均匀随机数；
3. 使用 Box-Muller 生成标准正态随机数；
4. 生成相关高斯变量 Z_S 和 Z_V；
5. 模拟资产价格路径和方差路径；
6. 计算欧式看涨期权 payoff；
7. 对多条路径进行 Monte Carlo 平均；
8. 输出价格、方差、标准误差、负方差次数和 checksum。
```

正式误差验证应由外部 harness 完成，例如固定随机种子后比较 baseline 和 mixed-precision 版本的价格、payoff 统计量、最终 `S/V` 统计量和负方差提议次数。

## 6. 程序主要流程

程序整体流程如下：

```text
1. 解析命令行参数；
2. 设置 S0、K、r、V0、kappa、theta、xi、rho、T；
3. 计算 dt 和 sqrt_dt；
4. 初始化随机数生成器；
5. 对每条 Monte Carlo 路径：
   1. 初始化 S = S0；
   2. 初始化 V = V0；
   3. 对每个时间步：
      1. 生成 Z_V；
      2. 生成独立正态变量；
      3. 根据 rho 构造 Z_S；
      4. 用当前 V 更新资产价格 S；
      5. 用 Heston 方差方程更新 V；
      6. 如 V_proposal < 0，记录负方差提议；
   4. 计算 payoff = max(S_T - K, 0)；
   5. 累加 payoff、payoff^2、final_S、final_V；
6. 计算折现期权价格；
7. 输出统计量。
```

简化伪代码为：

```text
for each path:
    S = S0
    V = V0

    for each time step:
        Z_V = gaussian_random()
        Z_ind = gaussian_random()
        Z_S = rho*Z_V + sqrt(1-rho^2)*Z_ind

        S = S * exp((r - 0.5*max(V,0))*dt
                    + sqrt(max(V,0))*sqrt_dt*Z_S)

        V = heston_variance_step(V, ...)

    payoff = max(S - K, 0)
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

该函数使用 Box-Muller 变换生成标准正态随机数。

核心计算包括：

```text
radius = sqrt(-2 * log(u1))
theta  = 2*pi*u2
z0 = radius * cos(theta)
z1 = radius * sin(theta)
```

该函数包含 `log()`、`sqrt()`、`sin()`、`cos()` 等数学函数，是函数级混合精度搜索的重要候选区域。

### 7.3 `heston_variance_step()`

```c
double heston_variance_step(double V,
                            double kappa,
                            double theta,
                            double xi,
                            double dt,
                            double sqrt_dt,
                            double Z_V,
                            int scheme,
                            int *negative_proposal)
```

该函数执行单步 Heston 方差更新，是程序最重要的数值敏感区域之一。

它负责：

```text
1. 计算 V_pos = max(V, 0)
2. 计算 sqrt_V = sqrt(V_pos)
3. 计算 drift
4. 计算 diffusion
5. 形成 V_proposal
6. 判断 V_proposal 是否为负
7. 根据 scheme 修复负方差
```

其中 `V_proposal < 0` 是一个重要分支，混合精度转换可能改变该分支是否触发。

### 7.4 `simulate_heston_payoff()`

```c
HestonPathResult simulate_heston_payoff(...)
```

该函数模拟一条完整 Heston 路径，并返回该路径的欧式看涨期权 payoff。

它同时处理：

```text
相关高斯变量生成
资产价格指数更新
方差递推更新
负方差次数统计
最终 payoff 计算
```

这是路径级核心热点函数。

### 7.5 `price_heston_MC()`

```c
HestonMCResult price_heston_MC(...)
```

该函数执行完整 Monte Carlo 定价，对所有路径的 payoff 进行累加，并计算价格、方差、标准误差、最终资产价格均值、最终方差均值和负方差提议次数。

输出结构包括：

```text
price
mean_payoff
stderr_price
payoff_variance
mean_final_S
mean_final_V
negative_variance_proposals
payoff_sum
payoff_sq_sum
final_S_sum
final_V_sum
checksum_payoff
```

## 8. 编译方式

推荐编译命令：

```bash
gcc -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    heston_mc_target.c -lm -o heston_mc
```

或使用 Clang：

```bash
clang -std=c99 -O3 -fno-fast-math -ffp-contract=off \
    heston_mc_target.c -lm -o heston_mc
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
./heston_mc [S0] [K] [r] [V0] [kappa] [theta] [xi] [rho] [T] [N_steps] [N_paths] [iters] [scheme] [seed]
```

默认参数为：

```text
S0 = 100
K = 100
r = 0.03
V0 = 0.04
kappa = 2.0
theta = 0.04
xi = 0.5
rho = -0.7
T = 1.0
N_steps = 252
N_paths = 200000
iters = 3
scheme = 0
seed = 1
```

标准测试：

```bash
./heston_mc 100 100 0.03 0.04 2.0 0.04 0.5 -0.7 1.0 252 200000 3 0 1
```

高 vol-of-vol 压力测试：

```bash
./heston_mc 100 100 0.03 0.04 1.0 0.04 1.0 -0.7 1.0 252 500000 3 1 1
```

full truncation 测试：

```bash
./heston_mc 100 100 0.03 0.04 2.0 0.04 0.5 -0.7 1.0 252 200000 3 2 1
```

## 10. 参数说明

| 参数      | 含义                       |
| --------- | -------------------------- |
| `S0`      | 初始资产价格               |
| `K`       | 执行价格                   |
| `r`       | 无风险利率                 |
| `V0`      | 初始方差                   |
| `kappa`   | 方差均值回复速度           |
| `theta`   | 长期方差均值               |
| `xi`      | 方差波动率，vol-of-vol     |
| `rho`     | 资产噪声和方差噪声相关系数 |
| `T`       | 到期时间                   |
| `N_steps` | 时间步数                   |
| `N_paths` | Monte Carlo 路径数         |
| `iters`   | 重复运行次数               |
| `scheme`  | 负方差修复方案             |
| `seed`    | 随机数种子                 |

`rho` 必须位于 `[-1, 1]`，`scheme` 必须为 `0`、`1` 或 `2`。

## 11. 输出说明

程序输出如下字段：

```text
benchmark
S0
K
r
V0
kappa
theta
xi
rho
T
N_steps
N_paths
iters
scheme
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
mean_final_S
mean_final_V
negative_variance_proposals
payoff_sum
payoff_sq_sum
final_S_sum
final_V_sum
checksum_payoff
```

主要字段含义如下：

| 输出项                        | 含义                               |
| ----------------------------- | ---------------------------------- |
| `price`                       | 折现后的欧式看涨期权价格           |
| `mean_payoff`                 | 平均 payoff                        |
| `stderr_price`                | Monte Carlo 采样标准误差           |
| `payoff_variance`             | payoff 样本方差                    |
| `mean_final_S`                | 终端资产价格均值                   |
| `mean_final_V`                | 终端方差均值                       |
| `negative_variance_proposals` | 方差更新中 `V_proposal < 0` 的次数 |
| `payoff_sum`                  | payoff 总和                        |
| `payoff_sq_sum`               | payoff 平方和                      |
| `final_S_sum`                 | 终端资产价格总和                   |
| `final_V_sum`                 | 终端方差总和                       |
| `checksum_payoff`             | 加权 payoff checksum               |

其中 `negative_variance_proposals` 是 Heston 模型中特别重要的输出项。混合精度变化可能改变负方差提议次数，从而改变路径分布和最终价格。

## 12. 核心热点函数

程序的主要热点函数包括：

| 函数                       | 作用                    | 热点原因                     |
| -------------------------- | ----------------------- | ---------------------------- |
| `price_heston_MC()`        | 多路径 Monte Carlo 定价 | 外层路径循环                 |
| `simulate_heston_payoff()` | 单路径 Heston 模拟      | 内层时间步循环               |
| `heston_variance_step()`   | 方差过程更新            | 包含 `sqrt/fmax/fabs` 和分支 |
| `gaussian_random()`        | 正态随机数生成          | 包含 `log/sqrt/sin/cos`      |
| `rng_uniform01()`          | 均匀随机数生成          | 每个高斯变量依赖它           |

运行时间主要集中在：

```text
price_heston_MC()
  -> simulate_heston_payoff()
      -> gaussian_random()
      -> heston_variance_step()
      -> exp()
```

## 13. 数值敏感点

该程序包含多个典型混合精度敏感点。

### 13.1 方差非负性

方差 `V` 理论上应非负，但 Euler 离散可能产生负值。低精度可能改变 `V_proposal` 的符号，从而改变截断或反射分支。

### 13.2 `sqrt(max(V,0))`

当 `V` 接近 0 时，`sqrt(max(V,0))` 对小误差敏感。若精度改变导致 `V` 从正变为 0 或从 0 变为正，扩散项会发生突变。

### 13.3 方差 drift 和 diffusion

方差更新中：

```text
drift = kappa*(theta - V)*dt
diffusion = xi*sqrt(V)*sqrt(dt)*Z_V
```

这两个项可能相互抵消。低精度可能影响均值回复和波动项的平衡。

### 13.4 相关高斯变量

程序使用：

```text
Z_S = rho*Z_V + sqrt(1-rho^2)*Z_independent
```

当 `rho` 接近 ±1 时，`sqrt(1-rho^2)` 变小，对精度更敏感。

### 13.5 资产价格指数更新

资产路径更新为：

```text
S *= exp((r - 0.5*V)*dt + sqrt(V)*sqrt(dt)*Z_S)
```

`exp()` 的输入误差会影响增长因子，而 `S` 是递推状态，误差会沿路径传播。

### 13.6 payoff 分支

payoff 为：

```text
max(S_T - K, 0)
```

当 `S_T` 接近 `K` 时，微小误差可能改变 payoff 是否为正。

### 13.7 跨路径归约

最终价格依赖大量路径 payoff 的求和。`N_paths` 很大时，归约误差会累积。

## 14. 混合精度搜索对象

该程序适合变量级和函数级混合精度工具搜索。重点对象包括：

| 对象                | 说明             | 敏感性   |
| ------------------- | ---------------- | -------- |
| `S`                 | 资产价格递推状态 | 极高     |
| `V`                 | 方差递推状态     | 极高     |
| `V_pos`             | 非负截断后的方差 | 高       |
| `sqrt_V`            | 方差平方根       | 高       |
| `drift`             | 方差漂移项       | 高       |
| `diffusion`         | 方差扩散项       | 高       |
| `V_proposal`        | 修复前方差       | 极高     |
| `V_new`             | 修复后方差       | 极高     |
| `Z_V`, `Z_S`        | 相关高斯随机变量 | 高       |
| `exponent_arg`      | `exp()` 输入     | 极高     |
| `growth`            | 资产增长因子     | 高       |
| `payoff_sum`        | payoff 总和      | 高       |
| `negative_proposal` | 负方差分支统计   | 分支敏感 |

最重要的变量通常是 `V`、`V_proposal`、`S`、`exponent_arg` 和 `payoff_sum`。

## 15. 推荐测试配置

标准 Heston 测试：

```bash
./heston_mc 100 100 0.03 0.04 2.0 0.04 0.5 -0.7 1.0 252 200000 3 0 1
```

高 vol-of-vol 压力测试：

```bash
./heston_mc 100 100 0.03 0.04 1.0 0.04 1.0 -0.7 1.0 252 500000 3 0 1
```

反射法测试：

```bash
./heston_mc 100 100 0.03 0.04 1.0 0.04 1.0 -0.7 1.0 252 500000 3 1 1
```

full truncation 测试：

```bash
./heston_mc 100 100 0.03 0.04 2.0 0.04 0.5 -0.7 1.0 252 200000 3 2 1
```

强负相关测试：

```bash
./heston_mc 100 100 0.03 0.04 2.0 0.04 0.5 -0.95 1.0 252 200000 3 0 1
```

长时间步路径测试：

```bash
./heston_mc 100 100 0.03 0.04 2.0 0.04 0.5 -0.7 1.0 1000 200000 3 0 1
```

## 16. 外部误差验证建议

由于本程序不内置高精度 reference，建议外部 harness 按如下流程验证混合精度配置：

```text
1. 编译并运行原始 double baseline；
2. 固定 seed、N_steps、N_paths 和 scheme；
3. 保存 baseline 的 price、payoff_sum、checksum_payoff、mean_final_S、mean_final_V；
4. 编译并运行 mixed-precision 转换版本；
5. 使用完全相同的输入参数和 seed；
6. 比较价格、终端统计量、负方差提议次数和 checksum；
7. 检查误差是否满足阈值；
8. 比较运行时间判断是否加速。
```

推荐误差指标包括：

```text
abs_price_error
rel_price_error
payoff_sum_error
checksum_payoff_error
mean_final_S_error
mean_final_V_error
negative_variance_proposal_diff
stderr-aware_error_ratio
```

对于 Heston 模型，负方差提议次数差异非常重要。即使价格误差暂时不大，负方差分支行为改变也可能说明路径过程已经发生明显变化。

## 17. 作为 benchmark 的分类建议

该程序可以归类为：

```text
Monte Carlo finance benchmark
Heston stochastic volatility model
stochastic differential equation simulation
branch-sensitive variance process
recursive stochastic path simulation
```

在混合精度 benchmark 中，可以归入以下危险类型：

| 分类           | 说明                           |
| -------------- | ------------------------------ |
| 随机采样       | 正态随机数影响路径分布         |
| 递推状态       | `S` 和 `V` 跨时间步更新        |
| 非负约束       | 方差不能为负，修复分支敏感     |
| 平方根近零敏感 | `sqrt(max(V,0))` 对小误差敏感  |
| 指数路径更新   | `S *= exp(...)` 误差沿路径传播 |
| payoff 分支    | `max(S_T-K,0)` 对 ATM 情况敏感 |
| 大规模归约     | payoff 跨路径求和              |
| 相关随机变量   | `rho` 控制高斯变量耦合         |

## 18. 注意事项

1. `stderr_price` 是 Monte Carlo 采样误差，不是混合精度数值误差。
2. 比较混合精度配置时必须固定随机数种子。
3. `scheme` 必须固定，否则不同负方差处理方法会引入算法偏差。
4. 高 `xi` 会增加负方差提议次数，使程序更敏感。
5. `rho` 接近 ±1 时，相关高斯生成更敏感。
6. 不建议只比较 price，也应比较 `mean_final_V` 和 `negative_variance_proposals`。
7. 若工具搜索数学函数调用，应谨慎处理 `sqrt()`、`exp()`、`log()`、`sin()`、`cos()`。
8. 不建议开启 `-ffast-math`。

## 19. 总结

`heston_mc_target.c` 是一个面向混合精度研究的 Heston 随机波动率 Monte Carlo benchmark。它模拟资产价格和随机方差的耦合路径，并使用欧式看涨期权 payoff 进行定价。该程序的核心热点包括 `price_heston_MC()`、`simulate_heston_payoff()`、`heston_variance_step()`、`gaussian_random()` 和 `rng_uniform01()`。由于包含随机数生成、相关高斯变量、方差非负修复、平方根近零敏感、指数路径更新和大规模路径归约，该程序非常适合测试混合精度工具在金融随机模拟场景中的有效性。