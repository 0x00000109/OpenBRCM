# SPDX-License-Identifier: GPL-2.0-only
"""Source-level guard for RX DMA teardown after an unconfirmed disable."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RxDmaQuarantineContract(unittest.TestCase):
    def test_free_paths_require_confirmed_quiescence(self):
        rx = (ROOT / "src/ob_rx.c").read_text()
        dma = (ROOT / "src/ob_dma.c").read_text()
        core = (ROOT / "src/ob_core.c").read_text()
        free_fn = rx.split("void ob_rx_free_buffers", 1)[1].split("static void ob_rx_build_descriptors", 1)[0]
        self.assertIn("!ob_rx_dma_may_free(&hw->rx.dma_ownership)", free_fn)
        self.assertIn("ob_rx_dma_quarantine(&hw->rx.dma_ownership)", rx)
        self.assertIn("!ob_rx_dma_may_free(&hw->rx.dma_ownership)", dma)
        remove = core.split("void ob_remove", 1)[1]
        self.assertIn("ret = ob_rx_remove_begin(hw)", remove)
        self.assertIn("if (!ret)", remove)
        self.assertIn("ob_dma_free(hw)", remove)
        self.assertIn("resources quarantined", remove)

    def test_rx_lifecycle_apis_are_serialized_and_remove_is_sticky(self):
        rx = (ROOT / "src/ob_rx.c").read_text()
        core = (ROOT / "src/ob_core.c").read_text()
        self.assertIn("mutex_lock(&hw->rx_lifecycle_lock)", rx)
        self.assertIn("mutex_unlock(&hw->rx_lifecycle_lock)", rx)
        self.assertIn("ob_rx_dma_mark_removing(&hw->rx.dma_ownership)", rx)
        self.assertIn("ob_rx_dma_may_initialize(&hw->rx.dma_ownership)", rx)
        self.assertIn("refusing buffer free while RX tasklet lifetime is active", rx)
        alloc = core.split("hw = devm_kzalloc", 1)[1].split("bcma_set_drvdata", 1)[0]
        self.assertIn("mutex_init(&hw->rx_lifecycle_lock)", alloc)

    def test_device_lost_quarantine_is_written_after_tasklet_stops(self):
        rx = (ROOT / "src/ob_rx.c").read_text()
        quiesce = rx.split("static int ob_rx_quiesce_locked", 1)[1]
        quiesce = quiesce.split("int ob_rx_quiesce(", 1)[0]
        tasklet_kill = quiesce.index("tasklet_kill(&hw->rx.tasklet)")
        active_branch = quiesce.index("if (!hw->dev_lost)")
        self.assertNotIn("ob_rx_dma_quarantine",
                         quiesce[active_branch:tasklet_kill])
        self.assertGreater(quiesce.index("ob_rx_dma_quarantine", tasklet_kill),
                           tasklet_kill)

    def test_route_and_mask_teardown_waits_for_inflight_tasklet(self):
        rx = (ROOT / "src/ob_rx.c").read_text()
        quiesce = rx.split("static int ob_rx_quiesce_locked", 1)[1]
        quiesce = quiesce.split("int ob_rx_quiesce(", 1)[0]
        tasklet_kill = quiesce.index("tasklet_kill(&hw->rx.tasklet)")
        self.assertLess(quiesce.index("WRITE_ONCE(hw->rx.running, false)"),
                        quiesce.index("synchronize_irq"))
        self.assertGreater(quiesce.index("hw->rx.route = false"), tasklet_kill)
        self.assertGreater(quiesce.index("OB_D11_REG_MACINTMASK"), tasklet_kill)
        irq = rx.split("void ob_rx_irq(", 1)[1].split("static void ob_rx_tasklet", 1)[0]
        self.assertIn("READ_ONCE(hw->rx.running)", irq)

    def test_normal_probe_remains_rx_gated(self):
        core = (ROOT / "src/ob_core.c").read_text()
        probe = core.split("int ob_probe", 1)[1].split("void ob_remove", 1)[0]
        self.assertNotIn("ob_rx_init(hw)", probe)

    def test_rx_init_fails_before_allocating_without_durable_remove_owner(self):
        rx = (ROOT / "src/ob_rx.c").read_text()
        init = rx.split("int ob_rx_init(struct ob_hw *hw)", 1)[1]
        init = init.split("static int ob_rx_quiesce_locked", 1)[0]
        gate = "ob_rx_dma_activation_supported()"
        self.assertIn(gate, init,
                      "RX activation must fail closed until DMA quarantine "
                      "ownership survives BCMA/PCI teardown")
        self.assertIn("-EOPNOTSUPP", init)
        self.assertLess(init.index(gate), init.index("tasklet_setup"))
        self.assertLess(init.index(gate), init.index("ob_rx_alloc_buffers"))
        self.assertLess(init.index(gate), init.index("ob_rx_program"))

    def test_irq_teardown_closes_gate_before_optional_register_access(self):
        irq = (ROOT / "src/ob_irq.c").read_text()
        free_fn = irq.split("void ob_irq_free", 1)[1]
        free_fn = free_fn.split("\n}", 1)[0]
        self.assertLess(free_fn.index("WRITE_ONCE(hw->irq.active, false)"),
                        free_fn.index("if (!hw->dev_lost)"))
        self.assertLess(free_fn.index("if (!hw->dev_lost)"),
                        free_fn.index("synchronize_irq"))
        devlost_guard = free_fn.split("if (!hw->dev_lost)", 1)[1].split("\n\t}", 1)[0]
        self.assertIn("ob_d11_write32", devlost_guard)
        self.assertNotIn("else", free_fn[:free_fn.index("synchronize_irq")])
        handler = irq.split("static irqreturn_t ob_irq_handler", 1)[1]
        self.assertIn("READ_ONCE(hw->irq.active)", handler)


if __name__ == "__main__":
    unittest.main()
