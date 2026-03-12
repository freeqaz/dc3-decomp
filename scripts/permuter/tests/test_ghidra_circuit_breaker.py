"""Tests for Ghidra circuit breaker auto-reset behavior."""

from __future__ import annotations

from unittest.mock import patch

import scripts.permuter.ghidra_cache as gc


def _reset_circuit_breaker():
    """Reset all circuit breaker state to defaults."""
    gc._ghidra_consecutive_failures = 0
    gc._ghidra_circuit_open = False
    gc._ghidra_circuit_trip_time = 0.0
    gc._ghidra_reset_interval = 300.0
    gc._ghidra_backoff_multiplier = 1.0


class TestCircuitBreakerProbeAfterInterval:
    """Breaker allows a probe attempt after the reset interval elapses."""

    def setup_method(self):
        _reset_circuit_breaker()

    def teardown_method(self):
        _reset_circuit_breaker()

    def test_breaker_blocks_before_interval(self):
        """Breaker stays tripped when not enough time has passed."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            mock_time.time.return_value = 1000.0
            # Trip the breaker
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc._ghidra_circuit_open is True

            # Only 100s later — not enough (interval is 300s)
            mock_time.time.return_value = 1100.0
            assert gc.ghidra_circuit_tripped() is True

    def test_breaker_allows_probe_after_interval(self):
        """Breaker returns False (not tripped) after interval elapses."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            mock_time.time.return_value = 1000.0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc._ghidra_circuit_open is True

            # 300s later — interval elapsed
            mock_time.time.return_value = 1300.0
            assert gc.ghidra_circuit_tripped() is False

    def test_breaker_allows_probe_at_exact_interval(self):
        """Breaker allows probe at exactly the interval boundary."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            mock_time.time.return_value = 1000.0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()

            # Exactly 300s later
            mock_time.time.return_value = 1300.0
            assert gc.ghidra_circuit_tripped() is False


class TestExponentialBackoff:
    """Backoff multiplier doubles on each re-trip, capped at 16x."""

    def setup_method(self):
        _reset_circuit_breaker()

    def teardown_method(self):
        _reset_circuit_breaker()

    def test_first_trip_uses_base_interval(self):
        """First trip uses multiplier 1.0 (base interval)."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            mock_time.time.return_value = 1000.0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc._ghidra_backoff_multiplier == 1.0

    def test_retrip_doubles_backoff(self):
        """Re-tripping after a failed probe doubles the multiplier."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            # Initial trip at t=1000
            mock_time.time.return_value = 1000.0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc._ghidra_backoff_multiplier == 1.0

            # Probe window opens at t=1300, probe fails (3 more failures)
            mock_time.time.return_value = 1300.0
            assert gc.ghidra_circuit_tripped() is False  # probe allowed
            gc._ghidra_consecutive_failures = 0  # reset for re-trip counting
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc._ghidra_backoff_multiplier == 2.0

            # Next probe needs 600s (300 * 2.0)
            mock_time.time.return_value = 1300.0 + 599.0
            assert gc.ghidra_circuit_tripped() is True  # not enough time
            mock_time.time.return_value = 1300.0 + 600.0
            assert gc.ghidra_circuit_tripped() is False  # enough time

    def test_backoff_caps_at_16x(self):
        """Multiplier caps at 16.0 regardless of how many re-trips occur."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            mock_time.time.return_value = 0.0
            # Initial trip
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc._ghidra_backoff_multiplier == 1.0

            # Re-trip multiple times: 1 -> 2 -> 4 -> 8 -> 16 -> 16
            t = 0.0
            for expected_mult in [2.0, 4.0, 8.0, 16.0, 16.0]:
                t += gc._ghidra_reset_interval * gc._ghidra_backoff_multiplier
                mock_time.time.return_value = t
                assert gc.ghidra_circuit_tripped() is False
                gc._ghidra_consecutive_failures = 0
                for _ in range(gc.GHIDRA_MAX_FAILURES):
                    gc._ghidra_record_failure()
                assert gc._ghidra_backoff_multiplier == expected_mult


class TestRecoveryOnSuccess:
    """Successful probe closes breaker and resets multiplier."""

    def setup_method(self):
        _reset_circuit_breaker()

    def teardown_method(self):
        _reset_circuit_breaker()

    def test_success_closes_breaker(self):
        """Recording success closes the breaker."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            mock_time.time.return_value = 1000.0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc._ghidra_circuit_open is True

            gc._ghidra_record_success()
            assert gc._ghidra_circuit_open is False

    def test_success_resets_multiplier(self):
        """Recording success resets backoff multiplier to 1.0."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            # Trip and re-trip to get multiplier > 1
            mock_time.time.return_value = 1000.0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            mock_time.time.return_value = 1300.0
            gc._ghidra_consecutive_failures = 0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc._ghidra_backoff_multiplier == 2.0

            gc._ghidra_record_success()
            assert gc._ghidra_backoff_multiplier == 1.0

    def test_success_resets_failure_count(self):
        """Recording success resets consecutive failure count to 0."""
        gc._ghidra_consecutive_failures = 2
        gc._ghidra_record_success()
        assert gc._ghidra_consecutive_failures == 0

    def test_full_recovery_cycle(self):
        """Trip -> wait -> probe success -> breaker fully closed."""
        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            mock_time.time.return_value = 1000.0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()
            assert gc.ghidra_circuit_tripped() is True

            # Wait for interval
            mock_time.time.return_value = 1300.0
            assert gc.ghidra_circuit_tripped() is False  # probe allowed

            # Probe succeeds
            gc._ghidra_record_success()
            assert gc._ghidra_circuit_open is False
            assert gc._ghidra_backoff_multiplier == 1.0
            assert gc._ghidra_consecutive_failures == 0

            # Breaker is fully closed — no time check needed
            assert gc.ghidra_circuit_tripped() is False


class TestNormalOperation:
    """Breaker stays closed during normal operation."""

    def setup_method(self):
        _reset_circuit_breaker()

    def teardown_method(self):
        _reset_circuit_breaker()

    def test_not_tripped_initially(self):
        """Breaker is not tripped in initial state."""
        assert gc.ghidra_circuit_tripped() is False

    def test_not_tripped_after_partial_failures(self):
        """Breaker stays closed when failures are below threshold."""
        for _ in range(gc.GHIDRA_MAX_FAILURES - 1):
            gc._ghidra_record_failure()
        assert gc.ghidra_circuit_tripped() is False
        assert gc._ghidra_circuit_open is False

    def test_success_resets_partial_failures(self):
        """A success resets partial failure count, preventing trip."""
        gc._ghidra_record_failure()
        gc._ghidra_record_failure()
        gc._ghidra_record_success()
        gc._ghidra_record_failure()
        assert gc.ghidra_circuit_tripped() is False
        assert gc._ghidra_circuit_open is False

    def test_set_ghidra_retry_interval(self):
        """set_ghidra_retry_interval overrides the base interval."""
        gc.set_ghidra_retry_interval(60.0)
        assert gc._ghidra_reset_interval == 60.0

        with patch("scripts.permuter.ghidra_cache.time") as mock_time:
            mock_time.time.return_value = 1000.0
            for _ in range(gc.GHIDRA_MAX_FAILURES):
                gc._ghidra_record_failure()

            # 60s later — should allow probe with custom interval
            mock_time.time.return_value = 1060.0
            assert gc.ghidra_circuit_tripped() is False
