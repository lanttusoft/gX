#!/usr/bin/env bash
# gX resource + copyright audit — runs all validators, fails CI on real issues.
# Report-only: resource_deps --unused and shader_uniform_validator (heuristic).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FAIL=0

run() {
    local name="$1"; shift
    echo "== [gX audit] $name =="
    if ! "$@" > /tmp/gx_audit_$name.log 2>&1; then
        echo "  FAILED: $name (see /tmp/gx_audit_$name.log)"
        FAIL=1
    else
        tail -1 /tmp/gx_audit_$name.log | sed 's/^/  /'
    fi
}

run copyright     python3 scripts/copyright_audit.py --strict
run provenance    python3 scripts/provenance_audit.py --strict
run resources     python3 scripts/resource_validator.py
run deps          python3 scripts/resource_deps.py --missing
run models        python3 scripts/model_format_validator.py
#opencode: synth wavetable fixtures must match their generator (byte-exact).
run wavetables    python3 scripts/gen_wavetables.py --check
run shaders       python3 scripts/shader_compile_check.py
run enums         python3 scripts/enum_coverage.py --strict
run api           python3 scripts/api_coverage.py --strict

# heuristic / informational — report only
python3 scripts/resource_deps.py --unused > /tmp/gx_audit_unused.log 2>&1
echo "== [gX audit] unused-files (informational): $(grep -cE '^  [a-z]' /tmp/gx_audit_unused.log) files"
python3 scripts/shader_uniform_validator.py > /tmp/gx_audit_uniforms.log 2>&1
echo "== [gX audit] uniforms (informational): $(grep -c 'NOT referenced' /tmp/gx_audit_uniforms.log) heuristic warnings"

[ "$FAIL" -eq 0 ] && echo "== [gX audit] OK ==" || { echo "== [gX audit] FAILED =="; exit 1; }
