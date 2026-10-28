#!/bin/sh
# lit 测试用的 mock opencode：忽略参数，走 mock HTTP/SSE 服务，
# 回答内嵌一个合法的 Tier A DSL 策略（eval_uniform float 后结束）。
# 由 %S/opencode_mock_server.py 提供 /api/session、/api/event 等最小接口。
exec python3 "$(dirname "$0")/../opencode_mock_server.py" serve "$@"
