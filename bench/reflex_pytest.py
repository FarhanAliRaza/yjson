"""Run the PR's unchanged tests with mojson substituted at the dump boundary."""

from reflex_codec import install


def pytest_configure(config):
    """Install the experiment before test collection."""
    install()
