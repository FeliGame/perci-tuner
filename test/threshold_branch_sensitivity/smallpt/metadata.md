# smallpt 程序说明

## 1. 程序简介

源程序来源：https://www.kevinbeason.com/smallpt/

`smallpt` 是一个纯 C 实现的 Monte Carlo 路径追踪渲染程序，改写自 smallpt 风格的 C++ 路径追踪器。程序模拟 Cornell Box 场景中的全局光照传播，通过大量随机采样估计每个像素的辐射亮度，并最终输出一张 PPM 格式图像。

该程序属于计算机图形学中的物理真实感渲染应用，核心计算类型为递归随机光线追踪。它适合作为 Monte Carlo 渲染、递归控制流、几何求交、分支敏感计算和混合精度误差传播分析的 benchmark 程序。

## 2. 程序功能

程序完成以下任务：

1. 构造一个 Cornell Box 场景；
2. 从相机向每个像素发射 primary rays；
3. 对每条光线执行球体求交；
4. 根据材质类型进行漫反射、镜面反射或折射路径采样；
5. 递归计算路径贡献；
6. 使用 Russian roulette 控制路径终止；
7. 对多个随机样本求平均；
8. 将最终渲染结果写入 PPM 图像文件。

默认场景包含 9 个球体：

- 6 个大球体用于近似 Cornell Box 的墙面、地板和天花板；
- 1 个镜面球；
- 1 个玻璃折射球；
- 1 个顶部发光球作为光源。

## 3. 编译方式

单线程编译：

```bash
gcc -std=c99 -O3 -DNO_PROGRESS smallpt -lm -o smallpt_c_fixed
```

OpenMP 并行编译：

```bash
gcc -std=c99 -O3 -fopenmp -DNO_PROGRESS smallpt -lm -o smallpt_c_fixed
```

使用 Clang 也可以：

```bash
clang -std=c99 -O3 -fopenmp -DNO_PROGRESS smallpt -lm -o smallpt_c_fixed
```

其中：

- `-std=c99`：使用 C99 标准；
- `-O3`：开启高等级优化；
- `-fopenmp`：启用 OpenMP 并行渲染；
- `-lm`：链接数学库；
- `-DNO_PROGRESS`：关闭渲染进度输出，减少多线程打印干扰。

## 4. 运行方式

程序运行格式为：

```bash
./smallpt_c_fixed [spp] [width] [height] [output.ppm]
```

参数含义：

| 参数         | 含义                           |
| ------------ | ------------------------------ |
| `spp`        | 每像素采样数 samples per pixel |
| `width`      | 输出图像宽度                   |
| `height`     | 输出图像高度                   |
| `output.ppm` | 输出图像文件名                 |

示例：

```bash
./smallpt_c_fixed 16 320 240 image.ppm
```

该命令表示以 320×240 分辨率渲染图像，每像素采样数为 16，并将结果写入 `image.ppm`。

快速测试可以使用：

```bash
./smallpt_c_fixed 4 64 48 test.ppm
```

高质量渲染可以使用：

```bash
./smallpt_c_fixed 5000 1024 768 image.ppm
```

需要注意的是，`5000 spp` 和 `1024×768` 的计算量非常大，运行时间会明显增加。

## 5. 输出文件

程序输出 PPM 格式图像文件，例如：

```bash
image.ppm
```

输出格式为 ASCII PPM，即：

```text
P3
width height
255
R G B R G B ...
```

可以使用支持 PPM 的工具查看，例如：

```bash
display image.ppm
```

或转换为 PNG：

```bash
convert image.ppm image.png
```

## 6. 核心数据结构

程序主要使用以下结构体：

```c
typedef struct {
    double x, y, z;
} Vec;
```

`Vec` 表示三维向量，也用于表示 RGB 颜色。

```c
typedef struct {
    Vec o, d;
} Ray;
```

`Ray` 表示一条光线，其中 `o` 是光线起点，`d` 是单位方向。

```c
typedef enum {
    DIFF,
    SPEC,
    REFR
} Refl_t;
```

`Refl_t` 表示材质类型：

- `DIFF`：漫反射材质；
- `SPEC`：理想镜面反射材质；
- `REFR`：理想玻璃折射材质。

```c
typedef struct {
    double rad;
    Vec p;
    Vec e;
    Vec c;
    Refl_t refl;
} Sphere;
```

`Sphere` 表示球体对象：

- `rad`：半径；
- `p`：球心位置；
- `e`：自发光颜色；
- `c`：表面颜色或反射率；
- `refl`：材质类型。

## 7. 核心函数说明

### 7.1 向量运算函数

程序将原 C++ 版本中的运算符重载改成了 C 函数：

```c
vec_add()
vec_sub()
vec_scale()
vec_mult()
vec_dot()
vec_cross()
vec_norm()
```

这些函数分别实现向量加法、减法、数乘、逐元素乘法、点积、叉积和归一化。

### 7.2 `sphere_intersect()`

```c
static double sphere_intersect(const Sphere *s, const Ray *r)
```

该函数计算光线与球体的最近交点距离。如果没有交点，则返回 `0.0`。

其核心是解光线-球体二次方程：

```text
|o + t d - p|² = R²
```

其中：

- `o` 是光线起点；
- `d` 是光线方向；
- `p` 是球心；
- `R` 是球体半径；
- `t` 是交点距离。

该函数是程序的几何求交核心，也是运行热点之一。

### 7.3 `scene_intersect()`

```c
static int scene_intersect(const Ray *r, double *t, int *id)
```

该函数遍历场景中的所有球体，寻找与当前光线最近的交点。

如果命中，则返回 `1`，并通过 `t` 返回交点距离，通过 `id` 返回命中球体编号；如果没有命中，则返回 `0`。

### 7.4 `radiance()`

```c
static Vec radiance(const Ray *r, int depth, uint64_t *rng_state)
```

这是程序最核心的递归路径追踪函数。它根据输入光线计算该方向上的辐射亮度。

主要流程如下：

1. 判断光线是否与场景相交；
2. 若未命中，返回黑色；
3. 若命中，取得命中点、法线和材质；
4. 根据材质类型分支处理：
   - 漫反射：进行余弦加权半球采样；
   - 镜面反射：沿理想反射方向递归；
   - 折射材质：根据 Snell 定律和 Fresnel 近似计算反射/折射贡献；
5. 使用 Russian roulette 控制递归路径终止；
6. 返回发光项和反射/折射贡献的累积结果。

`radiance()` 是程序的主要热点函数。

### 7.5 `init_scene()`

```c
static void init_scene(void)
```

该函数初始化 Cornell Box 场景，包括墙面、镜面球、玻璃球和顶部光源。

### 7.6 `main()`

`main()` 完成以下工作：

1. 解析运行参数；
2. 初始化场景；
3. 分配并初始化图像缓冲区；
4. 设置相机；
5. 遍历每个像素和子像素；
6. 对每个像素执行多样本路径追踪；
7. 将结果写入 PPM 文件；
8. 释放内存并退出程序。

## 8. 核心算法流程

程序的核心算法为 Monte Carlo Path Tracing。其简化流程如下：

```text
for each pixel:
    for each subpixel:
        for each sample:
            generate primary camera ray
            color += radiance(ray)
    average samples
    write pixel color
```

其中 `radiance()` 内部递归执行：

```text
ray -> intersect scene
    -> if miss: return black
    -> if hit diffuse: sample random hemisphere direction
    -> if hit mirror: compute reflection ray
    -> if hit glass: compute reflection/refraction rays
    -> recursively trace next ray
    -> accumulate emitted and reflected radiance
```

## 9. 本 C 版本修正的问题

相对于原始 C++ 代码，该版本修正了以下问题：

### 9.1 将 `_start()` 改为 `main()`

原代码使用：

```cpp
extern "C" void _start()
```

这不适合作为普通用户程序入口。新版本改为标准 C 入口：

```c
int main(int argc, char **argv)
```

### 9.2 修复图像缓冲未初始化问题

原代码中使用 `malloc()` 分配图像缓冲，但没有初始化：

```cpp
Vec *c = (Vec*)malloc(sizeof(Vec)*w*h);
c[i] = c[i] + ...
```

这会导致 `c[i]` 初始值不确定，从而污染图像结果。

新版本改为：

```c
Vec *c = calloc((size_t)w * (size_t)h, sizeof(Vec));
```

确保所有像素初始为黑色。

### 9.3 替换 C++ 运算符重载

原代码依赖 C++ 运算符，例如：

```cpp
a + b
a - b
a * s
a % b
```

新版本改为 C 函数调用，例如：

```c
vec_add(a, b)
vec_sub(a, b)
vec_scale(a, s)
vec_cross(a, b)
```

### 9.4 替换 `erand48()`

原代码使用 `erand48()` 作为随机数生成器。新版本改用基于 `uint64_t` 的轻量级 per-pixel 随机数生成器，使程序更可移植，也更适合 OpenMP 并行执行。

### 9.5 增加 Russian roulette 防护

在路径终止逻辑中，新版本对最大反射率 `p <= 0.0` 的情况进行了保护，避免出现 `1.0 / p` 的除零风险。

### 9.6 增加参数解析和错误检查

新版本支持命令行指定采样数、图像宽度、高度和输出文件名，并对内存分配和文件打开失败进行了检查。

## 10. 主要热点函数

程序的主要性能热点包括：

| 热点函数             | 作用          | 热点原因                             |
| -------------------- | ------------- | ------------------------------------ |
| `radiance()`         | 递归路径追踪  | 每条光线都会调用，且会递归生成新光线 |
| `scene_intersect()`  | 场景求交      | 每条光线都要遍历所有球体             |
| `sphere_intersect()` | 光线-球体求交 | 调用次数极高，包含点积和平方根       |
| `vec_norm()`         | 向量归一化    | 高频调用，包含 `sqrt()`              |
| `rng_uniform()`      | 随机数生成    | 每个样本和路径分支都需要随机数       |

其中最主要的运行时间通常集中在：

```text
radiance() -> scene_intersect() -> sphere_intersect()
```

## 11. 数值敏感点

该程序非常适合用于混合精度和数值敏感性测试，原因是它包含大量分支敏感与递归误差传播行为。

主要敏感点包括：

### 11.1 光线-球体判别式

```c
det = b * b - vec_dot(op, op) + s->rad * s->rad;
```

如果 `det` 在 0 附近，微小精度误差可能导致光线从“命中”变为“未命中”，从而改变整条路径。

### 11.2 自相交阈值

```c
eps = 1e-4;
```

该阈值用于避免光线从表面再次命中自身。精度变化可能导致错误自相交或漏交。

### 11.3 折射全内反射判断

```c
cos2t = 1.0 - nnt * nnt * (1.0 - ddn * ddn);
```

如果 `cos2t` 接近 0，精度误差可能改变折射与全内反射分支。

### 11.4 Russian roulette 分支

Russian roulette 通过随机数决定路径是否继续。微小数值误差可能改变路径概率权重或随机分支结果。

### 11.5 递归路径传播

每次递归都会生成新的光线方向和交点。早期的小误差可能在后续多次反射、折射中放大，最终影响像素颜色。

## 12. 作为 benchmark 的分类

该程序可以归类为：

```text
Monte Carlo rendering
recursive stochastic ray tracing
branch-sensitive geometric computation
path-tracing mini-application
```

在混合精度 benchmark 中，它可以代表以下数值特征：

1. 随机采样；
2. 递归计算；
3. 几何判别敏感；
4. 分支敏感控制流；
5. 大量 `sqrt()`、`sin()`、`cos()`、`pow()` 调用；
6. 图像级误差输出。

## 13. 注意事项

1. 高 `spp` 会显著增加运行时间。

2. 调试时建议使用较小分辨率和较低采样数。

3. 若用于性能测试，建议关闭进度输出：

   ```bash
   -DNO_PROGRESS
   ```

4. 若用于 OpenMP 并行测试，建议固定线程数：

   ```bash
   export OMP_NUM_THREADS=8
   ```

5. PPM 文件较大时，可以转换为 PNG 便于查看：

   ```bash
   convert image.ppm image.png
   ```

## 14. 总结

`smallpt` 是一个纯 C 版本的 smallpt 风格路径追踪器。它通过 Monte Carlo 路径追踪方法模拟 Cornell Box 场景中的全局光照传播，核心计算集中在递归辐射亮度函数 `radiance()`、场景求交函数 `scene_intersect()` 和球体求交函数 `sphere_intersect()`。该程序既可以作为计算机图形学渲染示例，也适合作为混合精度和数值敏感性 benchmark，尤其适合覆盖随机采样、递归误差传播、几何判别敏感和分支敏感控制流等典型场景。