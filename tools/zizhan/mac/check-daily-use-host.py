#!/usr/bin/env python3
"""Run zizhan thermal inventory and candidate-policy regressions without a phone."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]


def main():
    for name in ("keyboard-scenario.py", "kmem-range-diagnostics.py", "load-command-padding.py", "adt-cpu-clusters.py", "hall-state.py", "hall-contract.py", "hall-probe.py", "slpi-preflight.py", "slpi-shared-resources.py", "slpi-transport.py", "slpi-transport-platform.py", "slpi-power.py", "slpi-cmd-db.py", "slpi-rpmh-batch.py", "rpmh-receipt.py", "statusbar-panel.py", "cs35l41-identity.py", "cs35l41-se15.py", "cs35l41-setup.py", "cs35l41-timing.py", "cs35l41-inputs.py", "cs35l41-load-plan.py", "cs35l41-ram-transfer.py", "cs35l41-ram.py", "cs35l41-coefficients.py", "cs35l41-run.py", "cs35l41-prm.py", "cs35l41-prm-platform.py", "cs35l41-tdm.py", "cs35l41-pcm.py", "cs35l41-graph.py", "cs35l41-tone48.py", "cs35l41-stream.py", "cs35l41-audio-abi.py", "cs35l41-service.py", "cs35l41-service-gate.py", "cs35l41-flow.py", "zizhan-audio-plugin.py", "thermal-policy.py", "display-power.py", "cs35l41-dma.py", "cs35l41-dma-map.py", "cs35l41-dma-platform.py", "cs35l41-memory.py", "cs35l41-write.py", "cs35l41-batch.py", "cs35l41-ring.py", "cs35l41-zero.py", "cs35l41-zero-platform.py", "cs35l41-tone.py", "cs35l41-tone-platform.py", "cs35l41-pulse.py", "cs35l41-data-pin.py", "cs35l41-wire-platform.py", "cs35l41-output.py", "cs35l41-tdm-inputs.py", "cs35l41-ram-adt.py", "cs35l41-init-plan.py", "ipcc-router.py", "ipcc-review.py", "thermal-inventory.py", "tsens-probe.py", "xnu-patch-format.py",
                 "power-inject.py", "display-power-s23.py", "wait-android.py",
                 "bringup-policy.py",
                 "power-transaction.py",
                 "power-query.py", "power-inventory.py", "adsp-event-wait.py", "slpi-std.py",
                 "panel-program.py", "panel-pool.py", "zc-fence-target.py", "display-owner.py", "dsi-command.py", "dsi-domain.py", "userland-policy-audit.py",
                 "service-policy.py", "fat.py"):
        subprocess.run([sys.executable, str(ROOT / "tools/zizhan/tests" / name)],
                       check=True, timeout=60)


if __name__ == "__main__":
    main()
