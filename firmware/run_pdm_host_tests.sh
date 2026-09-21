#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
./run_pdm_efuse_sm_test.sh
./run_pdm_can_logic_test.sh
