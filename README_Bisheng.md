# Perci-Tuner(编译系统挑战赛2025定制版)文档（编译器：Bisheng LLVM-17.0.6）
Perci-Tuner是一个面向C语言的变量级混合精度动态搜索优化工具,支持包括数组、指针、结构体成员在内的多种数据类型转换、多精度C数学库转换、函数参数类型变换等。支持扩展多种不同动态搜索算法，支持命令一键式实现生成精度配置、多版本函数调用、浮点异常模式（需搭配MxP版本LLVM）、编译指导命令（需搭配MxP版本LLVM）等多种功能。

下载Bisheng LLVM-17.0.6，修改clang源码识别#pragma AutoMxPrec：
``` bash
cd llvm-project
git apply Bisheng_AutoMxPrec_diff.patch` 
./build.sh -j10 -b release -i
```

安装并配置好LLVM环境变量后，然后下载本仓库，配置环境变量：
``` bash
export PERCI_TUNER=/path/to/perci-tuner/
```

在hpl-ai.c文件开头插入以下代码，以解决头文件缺失问题：
```c
#include <string.h>
#include <math.h>
```

针对hpl-ai生成可执行文件output和精度配置文件config.json，初次执行会自动将Perci-Tuner编译为.so动态库：
``` bash
cd $PERCI_TUNER
./scripts/run.sh generate-config -lm /path/to/hpl-ai/
```

采用delta-debugging算法动态搜索最优配置，每次搜索以./hpl-ai 10 128 1280测试性能得分，最终生成最优配置文件、中间代码和可执行文件（分别为optimized_config.json、optimized_output.bc和optimized_output），以及搜索历史文件run_history.csv：
``` bash
./scripts/run.sh search-optimize -lm /path/to/hpl-ai/ -history
```

考虑搜索空间较大，搜索时间较长，我们已经打包了初赛对应的搜索配置文件和中间代码fp16_possible.zip，解压后的hpl-ai即初赛提交的可执行文件。我们支持直接指定配置文件生成可执行文件，即：
```bash
./scripts/run.sh change-by-config -target=fp16_possible.json -lm /path/to/hpl-ai/
```
这将生成根据指定配置文件修改后的中间代码changed_output.bc和可执行文件changed_output。
