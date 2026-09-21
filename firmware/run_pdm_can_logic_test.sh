#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -Werror -O0 -g -o /tmp/pdm_can_logic_test pdm_can_logic_test.cpp
/tmp/pdm_can_logic_test
