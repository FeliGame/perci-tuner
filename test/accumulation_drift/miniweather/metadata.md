````
代码来源：https://github.com/mrnorman/miniWeather/tree/main/c                            

适合在论文/benchmark 里怎么描述

可以这样写：

> miniWeather is a structured-grid atmospheric dynamics mini-application that solves dry, stratified, compressible, non-hydrostatic flow equations. The computation uses dimensionally split flux updates with fourth-order stencil-based interface reconstruction, hyperviscosity stabilization, and a three-stage low-storage Runge-Kutta time integrator. Its dominant kernels are the x- and z-direction tendency computations, which combine stencil access, equation-of-state pressure evaluation, flux divergence, and source-term updates.

中文可以写成：

> 该程序属于大气动力学/计算流体力学领域的结构网格 mini-application，模拟干空气分层可压缩非静力流动。核心算法采用方向分裂的通量更新方法，空间上使用四点 stencil 进行四阶界面重构，并引入 hyperviscosity 稳定项；时间上采用三阶段低存储 Runge-Kutta 推进。主要热点函数为 `compute_tendencies_x()` 和 `compute_tendencies_z()`，其内部包含 stencil 重构、状态方程压力计算、通量计算与通量散度更新，是程序运行时间的主要来源。xxxxxxxxxx 结构网格 PDE 求解器有限体积/有限差分流体模拟大气模型 mini-application
````

# miniweather_serial_c.c 程序说明

## 1. 程序简介

`miniweather_serial_c.c` 是一个单文件纯 C 版本的 miniWeather 程序，用于模拟二维干空气、分层、可压缩、非静力大气流体运动。程序基于结构网格，对流体状态变量进行时间推进，能够模拟热泡、密度流、重力波、碰撞热泡和边界注入等典型大气动力学场景。

该程序属于计算流体力学和大气动力学领域的 mini-application，可用于测试结构网格 PDE 求解、通量计算、stencil 访问、Runge-Kutta 时间推进、数值稳定性、混合精度误差传播和跨平台浮点行为。

## 2. 应用领域

该程序主要应用于以下领域：

```text
计算流体力学 CFD
大气动力学模拟
结构网格 PDE 求解
可压缩非静力流体模拟
HPC mini-application benchmark
混合精度与数值稳定性测试
```

从 benchmark 分类角度看，`miniweather_serial_c.c` 可以归为：

```text
structured-grid atmospheric dynamics mini-app
finite-volume / finite-difference-like PDE solver
stencil-based compressible flow simulation
```

它不是简单数值核，而是一个具有完整物理背景和多阶段时间推进流程的应用级 benchmark。

## 3. 物理模型

程序模拟二维大气流体状态，主要状态变量包括：

```text
ρ'      密度扰动
ρu      x 方向动量
ρw      z 方向动量
ρθ      密度 × 位温
```

程序中通常使用 4 个变量：

```c
ID_DENS  // density perturbation
ID_UMOM  // x-direction momentum
ID_WMOM  // z-direction momentum
ID_RHOT  // density × potential temperature
```

同时，程序维护一组静态背景水平均衡变量：

```text
hy_dens_cell
hy_dens_theta_cell
hy_dens_int
hy_dens_theta_int
hy_pressure_int
```

这些变量描述垂直方向上的 hydrostatic background state，即静力平衡背景场。真实流体状态由背景场与扰动状态共同组成。

## 4. 支持的初始化场景

程序支持多种大气测试场景。常见 `DATA_SPEC` 配置如下：

| `DATA_SPEC` | 场景名称        | 含义             |
| ----------- | --------------- | ---------------- |
| `1`         | collision       | 冷热热泡碰撞     |
| `2`         | thermal         | 上升热泡         |
| `3`         | gravity_waves   | 重力波           |
| `5`         | density_current | 密度流           |
| `6`         | injection       | 左边界冷空气注入 |

这些场景通过不同的初始温度扰动、速度扰动或边界条件构造，用于测试不同流体动力学行为。

## 5. 核心数值算法

程序的核心算法由三部分组成：

```text
空间离散：四点 stencil 界面重构 + 通量差分
时间推进：三阶段低存储 Runge-Kutta
方向分裂：x/z 方向交替 Strang splitting
```

### 5.1 空间离散

程序在 x 和 z 两个方向分别计算通量。每个方向的通量计算都使用四点 stencil 进行界面重构：

```text
vals = -q0/12 + 7q1/12 + 7q2/12 - q3/12
```

该形式对应四阶精度的界面插值。

同时，程序计算三阶差分形式的人工高阶黏性项：

```text
d3_vals = -q0 + 3q1 - 3q2 + q3
```

该项用于数值稳定化，控制高频振荡。

### 5.2 时间推进

程序采用三阶段 Runge-Kutta 时间推进。单方向半离散更新形式为：

```text
q*     = q[n] + dt/3 * rhs(q[n])
q**    = q[n] + dt/2 * rhs(q*)
q[n+1] = q[n] + dt   * rhs(q**)
```

每个完整时间步会分别对 x 和 z 方向执行三阶段推进。

### 5.3 方向分裂

程序使用交替方向分裂：

```text
step 1: x direction -> z direction
step 2: z direction -> x direction
step 3: x direction -> z direction
...
```

这种交替顺序用于降低固定方向分裂带来的偏差，可视为一种 Strang splitting 风格的方向分裂方法。

## 6. 程序主要流程

程序整体流程如下：

```text
1. 解析运行参数或使用默认配置
2. 初始化网格大小、时间步长和物理参数
3. 分配状态数组、通量数组、趋势数组和背景场数组
4. 初始化流体扰动状态和 hydrostatic background state
5. 计算初始质量和总能量
6. 进入主时间循环
7. 每一步执行 perform_timestep()
8. 时间推进结束后计算最终质量和总能量
9. 输出质量误差、能量误差和运行时间
10. 释放内存并退出
```

主循环的逻辑可以概括为：

```text
while etime < sim_time:
    if etime + dt > sim_time:
        dt = sim_time - etime
    perform_timestep(state, state_tmp, flux, tend, dt)
    etime += dt
```

## 7. 编译方式

使用 GCC 编译：

```bash
gcc -std=c99 -O3 -DNO_INFORM miniweather_serial_c.c -lm -o miniweather_serial_c
```

使用 Clang 编译：

```bash
clang -std=c99 -O3 -DNO_INFORM miniweather_serial_c.c -lm -o miniweather_serial_c
```

如果用于调试：

```bash
gcc -std=c99 -O0 -g miniweather_serial_c.c -lm -o miniweather_serial_c
```

如果用于性能测试：

```bash
gcc -std=c99 -O3 -march=native -DNO_INFORM miniweather_serial_c.c -lm -o miniweather_serial_c
```

其中：

| 编译参数        | 含义                            |
| --------------- | ------------------------------- |
| `-std=c99`      | 使用 C99 标准                   |
| `-O3`           | 开启高等级优化                  |
| `-march=native` | 针对本机 CPU 优化               |
| `-lm`           | 链接数学库                      |
| `-DNO_INFORM`   | 关闭每步进度输出，减少 I/O 干扰 |

## 8. 运行方式

程序可使用默认参数运行：

```bash
./miniweather_serial_c
```

也可以指定运行参数：

```bash
./miniweather_serial_c NX NZ SIM_TIME OUT_FREQ DATA_SPEC
```

参数含义如下：

| 参数        | 含义                               |
| ----------- | ---------------------------------- |
| `NX`        | x 方向网格数                       |
| `NZ`        | z 方向网格数                       |
| `SIM_TIME`  | 模拟总时间                         |
| `OUT_FREQ`  | 输出频率；若设为负数则关闭输出摘要 |
| `DATA_SPEC` | 初始化场景类型                     |

示例：

```bash
./miniweather_serial_c 400 200 100.0 -1.0 2
```

表示：

```text
NX = 400
NZ = 200
SIM_TIME = 100.0
OUT_FREQ = -1.0
DATA_SPEC = 2，即 thermal 上升热泡场景
```

快速测试可以使用：

```bash
./miniweather_serial_c 40 20 1.0 -1.0 2
```

性能测试可以使用：

```bash
./miniweather_serial_c 800 400 100.0 -1.0 2
```

## 9. 输出说明

程序主要输出：

```text
nx_glob, nz_glob
dx, dz
dt
CPU Time
d_mass
d_te
```

其中：

| 输出项             | 含义               |
| ------------------ | ------------------ |
| `nx_glob, nz_glob` | 全局网格尺寸       |
| `dx, dz`           | x/z 方向网格间距   |
| `dt`               | 稳定时间步长       |
| `CPU Time`         | 主时间循环运行时间 |
| `d_mass`           | 相对质量误差       |
| `d_te`             | 相对总能量误差     |

`d_mass` 和 `d_te` 是用于正确性检查的重要指标。对于数值模拟程序，完全逐位一致并不总是合理，但质量和能量误差应保持在可接受范围内。

## 10. 核心数据结构

程序使用一维连续数组存储二维网格和多个变量。

### 10.1 状态数组

```c
double *state;
double *state_tmp;
```

`state` 表示当前流体状态，`state_tmp` 表示 Runge-Kutta 中间状态。

数组逻辑维度为：

```text
NUM_VARS × (nz + 2*hs) × (nx + 2*hs)
```

其中 `hs` 是 halo 宽度，通常为 2。

### 10.2 通量数组

```c
double *flux;
```

逻辑维度为：

```text
NUM_VARS × (nz + 1) × (nx + 1)
```

用于存储单元界面上的通量。

### 10.3 趋势数组

```c
double *tend;
```

逻辑维度为：

```text
NUM_VARS × nz × nx
```

用于存储每个状态变量的时间导数或 RHS。

### 10.4 背景场数组

```c
double *hy_dens_cell;
double *hy_dens_theta_cell;
double *hy_dens_int;
double *hy_dens_theta_int;
double *hy_pressure_int;
```

这些数组存储 hydrostatic background state，用于构造真实密度、位温和压力。

## 11. 核心函数说明

### 11.1 `init()`

`init()` 负责初始化整个模拟，包括：

1. 设置网格尺寸；
2. 计算 `dx`、`dz`、`dt`；
3. 分配数组；
4. 初始化状态变量；
5. 初始化 hydrostatic background state；
6. 设置初始时间和输出计数器。

该函数只在程序开始时执行一次。

### 11.2 `perform_timestep()`

`perform_timestep()` 是单个完整时间步的主控函数。

它完成：

```text
x 方向三阶段 RK 更新
z 方向三阶段 RK 更新
或
z 方向三阶段 RK 更新
x 方向三阶段 RK 更新
```

每一步之后会切换方向顺序。

这是程序时间推进的核心调度函数。

### 11.3 `semi_discrete_step()`

`semi_discrete_step()` 执行一个方向上的半离散时间更新。

其主要流程为：

```text
1. 设置 halo 值
2. 计算该方向的 tendencies
3. 将 tendencies 应用到 state_out
```

核心更新公式为：

```text
state_out = state_init + dt * tend
```

该函数在每个完整时间步中会被调用 6 次，因此是重要热点之一。

### 11.4 `compute_tendencies_x()`

`compute_tendencies_x()` 计算 x 方向通量和趋势项。

主要步骤包括：

1. 从 state 中读取四点 stencil；
2. 进行四阶界面插值；
3. 计算密度、速度、位温和压力；
4. 构造 x 方向通量；
5. 通过通量差分计算 tendency。

该函数是程序的核心热点之一。

### 11.5 `compute_tendencies_z()`

`compute_tendencies_z()` 计算 z 方向通量和趋势项。

它与 x 方向类似，但额外包含：

```text
垂向压力扰动
上下边界 w = 0
垂向动量重力源项
```

其中垂向动量方程会加入重力项：

```text
tend[ID_WMOM] -= density_perturbation * grav
```

该函数也是程序最主要的计算热点之一。

### 11.6 `set_halo_values_x()`

该函数设置 x 方向 halo 值。串行版本中使用周期边界或指定边界逻辑直接复制边界数据，不再包含 MPI 通信。

### 11.7 `set_halo_values_z()`

该函数设置 z 方向 halo 值。z 方向不做 MPI 分解，使用物理边界条件：

```text
垂向速度 w 在上下边界设为 0
水平动量根据背景密度缩放
其他变量使用边界外推
```

### 11.8 `reductions()`

`reductions()` 计算全域质量和总能量：

```text
mass = ∑ ρ dx dz
te   = ∑ (kinetic energy + internal energy) dx dz
```

它用于模拟前后误差检查。

## 12. 主要热点函数

程序的热点函数大致如下：

| 热点等级 | 函数                     | 原因                                         |
| -------- | ------------------------ | -------------------------------------------- |
| 极高     | `compute_tendencies_x()` | 大规模 stencil、通量计算、压力计算           |
| 极高     | `compute_tendencies_z()` | 大规模 stencil、通量计算、压力计算、重力源项 |
| 高       | `semi_discrete_step()`   | 每个时间步调用 6 次，全数组更新              |
| 中       | `set_halo_values_x()`    | halo 拷贝，内存访问为主                      |
| 中       | `set_halo_values_z()`    | 边界条件设置，内存访问为主                   |
| 中       | `reductions()`           | 全网格归约，但通常只在开始和结束调用         |
| 低到中   | `init()`                 | 初始化成本较高，但只执行一次                 |

程序运行时间主要集中在：

```text
perform_timestep()
  -> semi_discrete_step()
      -> compute_tendencies_x()
      -> compute_tendencies_z()
```

## 13. 数值敏感点

该程序具有较强的数值敏感性，适合用于混合精度 benchmark。

### 13.1 压力计算

压力计算使用状态方程：

```text
p = C0 * pow(r * t, gamm)
```

该表达式是程序中最敏感的非线性计算之一。`r*t` 的误差会经过 `pow()` 放大，并进一步影响通量和动量更新。

### 13.2 通量重构

四点 stencil 插值：

```text
-q0/12 + 7q1/12 + 7q2/12 - q3/12
```

对相邻网格值进行加权组合。若状态变量变化剧烈，低精度可能引入局部重构误差。

### 13.3 Hyperviscosity

人工黏性项：

```text
-q0 + 3q1 - 3q2 + q3
```

包含加减抵消结构，对精度较敏感。低精度下可能改变数值耗散强度。

### 13.4 重力源项

垂向动量中的重力项与密度扰动相关：

```text
tend[ID_WMOM] -= state[ID_DENS] * grav
```

若密度扰动精度不足，可能影响垂向速度和能量演化。

### 13.5 质量与能量归约

`reductions()` 中的质量和能量累加属于全局归约。若使用低精度累加，误差可能随网格规模增长。

## 14. 本 C 版本修正的问题

相对于原始 miniWeather 程序，该 C 版本进行了以下修改和修正：

### 14.1 删除 MPI 逻辑

原程序包含 MPI 初始化、通信和归约接口。串行 C 版本删除了所有 MPI 相关代码，包括：

```text
MPI_Init
MPI_Barrier
MPI_Allreduce
MPI_Finalize
MPI rank/domain decomposition
```

程序现在作为单进程程序运行。

### 14.2 删除 PNetCDF 输出逻辑

原程序使用 PNetCDF 输出 NetCDF 文件。串行版本删除了所有 PNetCDF 相关代码，避免外部库依赖。

如需输出，该版本可使用简单摘要输出，而不依赖并行文件 I/O。

### 14.3 C++ 语法改为 C 语法

原程序包含 C++ 头文件和 `std::chrono` 计时逻辑。C 版本将其改为标准 C 代码，并使用 C 风格内存管理和计时函数。

### 14.4 修正 gravity waves 扰动重复累加问题

原逻辑中，重力波扰动可能在 `ll=0..NUM_VARS-1` 的循环内重复加到 `ID_WMOM` tendency 上，从而导致同一扰动被重复累加。

修正后，重力波扰动只在处理 `ID_WMOM` 时添加一次。

### 14.5 修正背景场 cell-average 计算

原始背景场 cell average 计算中，虽然循环了 Gauss-Legendre quadrature 点，但垂向坐标 `z` 没有使用 quadrature point 偏移，因此实际等价于重复计算 cell center。

修正后，`hy_dens_cell` 和 `hy_dens_theta_cell` 使用正确的 quadrature 点计算 cell average。

### 14.6 增加内存分配检查

C 版本对主要数组分配进行检查，避免 `malloc()` 失败后继续访问空指针。

### 14.7 使用 `size_t` 进行数组大小计算

为降低大网格下整数溢出风险，数组分配大小和索引计算尽量使用 `size_t`。

## 15. 混合精度分析建议

如果将该程序用于混合精度调优，建议按照以下风险等级选择变量。

### 15.1 高风险变量

以下变量建议优先保持 double：

```text
r
t
p
dt
dx
dz
C0
gamm
hy_dens_cell
hy_dens_theta_cell
hy_pressure_int
mass
te
```

这些变量直接影响状态方程、时间推进稳定性和全局守恒量。

### 15.2 中风险变量

以下变量可以尝试局部降精度，但需要仔细验证：

```text
vals
d3_vals
flux
tend
u
w
```

这些变量处于通量计算和 tendency 计算中，对最终结果有明显影响。

### 15.3 可实验降精度变量

以下变量可作为混合精度搜索候选：

```text
临时 stencil 数组
局部中间变量
部分 flux 临时值
部分 state perturbation 更新
```

但需要用质量误差、能量误差和场变量误差共同验证。

## 16. 推荐 benchmark 配置

快速正确性测试：

```bash
./miniweather_serial_c 40 20 1.0 -1.0 2
```

中等规模测试：

```bash
./miniweather_serial_c 400 200 100.0 -1.0 2
```

较大规模性能测试：

```bash
./miniweather_serial_c 800 400 100.0 -1.0 2
```

重力波测试：

```bash
./miniweather_serial_c 400 200 100.0 -1.0 3
```

密度流测试：

```bash
./miniweather_serial_c 400 200 100.0 -1.0 5
```

跨平台测试时建议固定：

```text
网格规模
模拟时间
DATA_SPEC
编译器
优化等级
浮点模型
输出格式
```

## 17. 作为 benchmark 的分类建议

该程序可以分类为：

```text
Atmospheric dynamics mini-app
Structured-grid PDE solver
Stencil-based compressible flow simulation
Finite-volume-like flux update
Runge-Kutta time integration
```

在混合精度 benchmark 中，可以归入以下危险类型：

| 分类           | 说明                      |
| -------------- | ------------------------- |
| stencil 访问   | x/z 方向四点 stencil 重构 |
| 非线性状态方程 | `pow(r*t, gamm)` 压力计算 |
| 加减抵消       | hyperviscosity 三阶差分   |
| 长时间递推     | 多时间步状态更新          |
| 守恒量敏感     | mass 和 total energy 误差 |
| 源项敏感       | 垂向重力源项              |
| 边界条件敏感   | halo 和上下边界处理       |

## 18. 注意事项

1. 网格尺寸越大，内存需求和运行时间越高。
2. `OUT_FREQ` 建议设为 `-1.0` 以关闭输出，避免 I/O 干扰性能测试。
3. 若用于混合精度测试，不建议只看最终状态，应同时检查质量误差、能量误差和场变量误差。
4. 对 `pow()`、`sqrt()` 等数学函数的精度变化需要特别关注。
5. 不同编译器优化选项可能改变浮点表达式求值顺序，从而影响长期模拟结果。
6. 如果将 `double` 改为 `long double`，需注意不同架构上 `long double` 的底层实现可能不同。

## 19. 总结

`miniweather_serial_c.c` 是一个纯 C、单文件、串行版 miniWeather 程序，模拟二维干空气分层可压缩非静力流体。其核心算法包括四点 stencil 通量重构、人工 hyperviscosity、三阶段 Runge-Kutta 时间推进和交替方向分裂。程序主要热点集中在 `compute_tendencies_x()`、`compute_tendencies_z()` 和 `semi_discrete_step()`。由于包含非线性压力计算、通量差分、长期时间推进和守恒量检查，该程序非常适合作为混合精度 benchmark 中的结构网格 PDE 应用级测试程序。