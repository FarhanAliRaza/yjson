"""Run production decoder regressions through the standalone adapter, plus teardown."""
import gc
import importlib.util
from pathlib import Path
import sys
import unittest

import _mojo_decoder
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests"))
import check_decoder
check_decoder.yjson = _mojo_decoder


class TestDecoder(check_decoder.TestDecoder):
    def test_context_teardown(self):
        # Separate module instances have independent key caches and release them.
        path = Path(_mojo_decoder.__file__)
        for _ in range(20):
            spec = importlib.util.spec_from_file_location("_mojo_decoder", path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            result = module.loads(b'{"cached":1}')
            self.assertEqual(result, {"cached": 1})
            key = next(iter(result))
            references = sys.getrefcount(key)
            del module
            gc.collect()
            self.assertEqual(sys.getrefcount(key), references - 1)
        gc.collect()
        self.assert_same(b'{"cached":2}')


if __name__ == "__main__":
    sys.setrecursionlimit(20000)
    unittest.main()
