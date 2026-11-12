#!/usr/bin/env bash
# Run pluginval at strictness 10 on a plugin. On Linux wrap the call in xvfb-run from the caller.
# usage: run_pluginval.sh <pluginval_exe> <plugin_path>
set -euo pipefail

exe="${1:?usage: run_pluginval.sh <pluginval_exe> <plugin_path>}"
plugin="${2:?usage: run_pluginval.sh <pluginval_exe> <plugin_path>}"
out="${PLUGINVAL_OUT:-pluginval-logs}"
mkdir -p "$out"

"$exe" --strictness-level 10 --timeout-ms 180000 --output-dir "$out" --verbose --validate "$plugin"
