"""Run the Reflex CLI after selecting this experiment's dump backend."""

import os

from reflex_codec import install
from reflex.reflex import cli

if os.environ.get("REFLEX_DUMP_BACKEND") == "mojson":
    install()

if __name__ == "__main__":
    cli()
