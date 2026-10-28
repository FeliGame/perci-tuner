// RUN: timeout -k 2 180 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize -algorithm=strategy -strategy="%S/../strategies/uniform_then_greedy.json" -max-evals=4 "%s" -lm' > %t.out 2>&1
// RUN: grep -q ">>> Strategy finished" %t.out
// RUN: ! grep -E 'fatal error:|: error:|error generated|^>>> Perci-Tuner Error' %t.out
// RUN: test -f %t/run_history.csv && test "$(wc -l < %t/run_history.csv)" -ge 3
// RUN: test -f %t/config/1.json

/* strategy_dsl_search.c -- M1 lit 测试：JSON DSL 策略端到端搜索
 * 验证：-algorithm=strategy + DSL 策略能完成基线评估与若干次精度降级评估，
 * 产出 run_history.csv 与 config/<run_id>.json，且无工具链错误。
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
