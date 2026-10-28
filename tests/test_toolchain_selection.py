#!/usr/bin/env python3
"""Check fallback policy without hiding explicit compiler or sanitizer choices."""
# Expectation basis: DOC: runner fallback policy; INFERRED: respect explicit toolchains.
# Rationale, sources and open questions: EXPECTATIONS.md
import contextlib
import io
import unittest
from unittest import mock

import run_tests


class ToolchainSelection(unittest.TestCase):
    def test_working_defaults_preserve_environment(self):
        environment = {'PATH': '/bin', 'ASAN_OPTIONS': 'detect_leaks=1'}
        with mock.patch.object(run_tests, 'probe_toolchain', return_value=None) as probe:
            self.assertEqual(run_tests.select_toolchain(environment), environment)
            probe.assert_called_once_with(environment)

    def test_broken_defaults_try_clang_without_changing_sanitizers(self):
        environment = {'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1'}
        output = io.StringIO()
        with mock.patch.object(run_tests, 'probe_toolchain',
                               side_effect=['missing libasan', None]) as probe:
            with contextlib.redirect_stdout(output):
                selected = run_tests.select_toolchain(environment)
        self.assertEqual(selected, dict(environment, CC='clang', CXX='clang++'))
        self.assertEqual(probe.call_count, 2)
        self.assertNotIn('CC', environment)
        self.assertIn('missing libasan', output.getvalue())
        self.assertIn('Using CC=clang CXX=clang++', output.getvalue())

    def test_explicit_compiler_is_never_replaced(self):
        for environment in ({'CC': 'chosen-cc'}, {'CXX': 'chosen-cxx'}):
            with mock.patch.object(run_tests, 'probe_toolchain',
                                   return_value='missing libasan') as probe:
                with self.assertRaisesRegex(RuntimeError, 'no tests were run'):
                    run_tests.select_toolchain(environment)
                probe.assert_called_once_with(environment)

    def test_failed_fallback_reports_both_diagnostics(self):
        with mock.patch.object(run_tests, 'probe_toolchain',
                               side_effect=['GCC linker failed', 'Clang runtime failed']):
            with self.assertRaises(RuntimeError) as error:
                run_tests.select_toolchain({})
        self.assertIn('GCC linker failed', str(error.exception))
        self.assertIn('Clang runtime failed', str(error.exception))


if __name__ == '__main__':
    unittest.main()
