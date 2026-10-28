// RUN: timeout -k 2 180 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize -algorithm=strategy -strategy="%S/../strategies/greedy_reduce.py" -allow-generated-strategy -max-evals=4 "%s" -lm' > %t.out 2>&1
// RUN: grep -q ">>> Strategy finished" %t.out
// RUN: ! grep -E 'fatal error:|: error:|error generated|^>>> Perci-Tuner Error' %t.out
// RUN: test -f %t/run_history.csv && test "$(wc -l < %t/run_history.csv)" -ge 3

/* strategy_python_search.c -- M1 lit 测试：沙箱内 Python 策略端到端搜索
 * 验证：-allow-generated-strategy 下 Tier B Python 策略通过 JSON-RPC
 * 驱动评估器完成搜索，沙箱协议与评估计数工作正常。
 */
#include <stdio.h>
#include <math.h>

int main(void) {
    double acc = 0.0;
    double x = 0.125;
    double scale = 1e-4;
    for (int i = 0; i < 5000; i++) {
        acc += sin(x + (double)i * scale);
        x += 1e-3;
    }
    printf("%.17g\n", acc);
    return 0;
}
