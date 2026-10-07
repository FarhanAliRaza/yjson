"""Build glue for pyproject.toml: compiles the Mojo extension through build.sh.

Everything declarative lives in pyproject.toml. This file only teaches setuptools
how to produce the `yjson` extension module, which is compiled by Mojo rather
than by a C compiler. Requires `mojo` on PATH (`uv sync --only-group mojo`).
"""

import os
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

ROOT = Path(__file__).resolve().parent
SOURCES = [
    "src/yjson.mojo",
    "src/python_api.c",
    "src/decoder.c",
    "src/layout_probe.c",
]


class MojoBuildExt(build_ext):
    """Runs build.sh for the interpreter doing the build and collects its outputs."""

    def build_extension(self, ext: Extension) -> None:
        if shutil.which(os.environ.get("MOJO", "mojo")) is None:
            raise RuntimeError(
                "the Mojo compiler was not found on PATH; install it with "
                "`uv sync --only-group mojo` (or `pip install mojo==1.1.0`) and "
                "put its bin directory on PATH, or point MOJO at the executable"
            )
        env = dict(os.environ, PYTHON=sys.executable)
        subprocess.run(["bash", str(ROOT / "build.sh")], check=True, cwd=ROOT, env=env)

        suffix = sysconfig.get_config_var("EXT_SUFFIX")
        built = ROOT / "build" / f"{ext.name}{suffix}"
        if not built.is_file():
            raise RuntimeError(f"build.sh did not produce {built}")
        target = Path(self.get_ext_fullpath(ext.name))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built, target)
        # The stub sits next to the extension module so type checkers find it.
        shutil.copy2(ROOT / "src" / "yjson.pyi", target.parent / "yjson.pyi")


setup(
    ext_modules=[Extension("yjson", sources=SOURCES, depends=["build.sh", "src/decoder_powers.h"])],
    cmdclass={"build_ext": MojoBuildExt},
)
