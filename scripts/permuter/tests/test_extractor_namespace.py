"""Tests for namespace-aware function extraction.

Regression coverage for callers that pass a fully-qualified
``Namespace::Class::Method`` name when the source declares the function as
``Class::Method`` inside ``namespace Namespace { ... }``.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.extractor import extract_function


_SOURCE = b"""\
namespace DSP {
    class SynapseAPO {
    public:
        SynapseAPO();
        ~SynapseAPO();
        void X();
    };

    void SynapseAPO::X() { int z = 1; }
    SynapseAPO::SynapseAPO() {}
    SynapseAPO::~SynapseAPO() {}
}

namespace soundtouch {
    class FIRFilter {
        void setCoefficients();
    };
    void FIRFilter::setCoefficients() { int q = 2; }
}

void OtherFn() { int r = 1; }
"""


class NamespaceExtractionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.NamedTemporaryFile(suffix=".cpp", delete=False)
        cls._tmp.write(_SOURCE)
        cls._tmp.flush()
        cls.path = Path(cls._tmp.name)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.path.unlink()
        except OSError:
            pass

    def _assert_resolves(self, name: str, must_contain: bytes):
        ctx = extract_function(self.path, name)
        body = ctx.source_text(ctx.func_node).encode("utf-8")
        self.assertIn(must_contain, body, f"resolved wrong fn for {name!r}")

    def test_qualified_with_namespace(self):
        self._assert_resolves("DSP::SynapseAPO::X", b"int z = 1;")

    def test_qualified_without_namespace(self):
        self._assert_resolves("SynapseAPO::X", b"int z = 1;")

    def test_destructor_with_namespace(self):
        self._assert_resolves("DSP::SynapseAPO::~SynapseAPO", b"~SynapseAPO")

    def test_destructor_without_namespace(self):
        self._assert_resolves("SynapseAPO::~SynapseAPO", b"~SynapseAPO")

    def test_constructor_with_namespace(self):
        self._assert_resolves("DSP::SynapseAPO::SynapseAPO", b"SynapseAPO()")

    def test_soundtouch_with_namespace(self):
        self._assert_resolves(
            "soundtouch::FIRFilter::setCoefficients", b"int q = 2;"
        )

    def test_soundtouch_without_namespace(self):
        self._assert_resolves("FIRFilter::setCoefficients", b"int q = 2;")

    def test_free_function(self):
        self._assert_resolves("OtherFn", b"int r = 1;")

    def test_not_found_raises(self):
        with self.assertRaises(ValueError):
            extract_function(self.path, "NotAFunction")

    def test_partial_namespace_does_not_false_match(self):
        # No `DSP::Foo` defined — must NOT silently resolve via suffix.
        with self.assertRaises(ValueError):
            extract_function(self.path, "DSP::Foo")


if __name__ == "__main__":
    unittest.main()
