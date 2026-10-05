"""Select a dump backend at the PR's existing codec boundary.

The decoder remains orjson for framework utility loads and stdlib for incoming
Socket.IO events, exactly as in PR 6116. Mojson uses native socket serialization
without the PR's stdlib retry. No global orjson monkeypatch is used.
"""

import contextlib
import sys
from types import SimpleNamespace

import mojson
import orjson
from reflex_base.utils import format
from reflex.utils import format as legacy_format


MOJSON_PROXY = SimpleNamespace(**{
    name: getattr(orjson, name) for name in dir(orjson) if not name.startswith("__")
})
MOJSON_PROXY.dumps = mojson.dumps
_original_socket = format.orjson_dumps_socket


def _socket(obj, **kwargs):
    """Use native wire serialization whenever this integration is selected."""
    if format.orjson is MOJSON_PROXY:
        return mojson.dumps_socket(obj, default=format._orjson_default).decode()
    return _original_socket(obj, **kwargs)


def _socket_modules():
    """Include the upload writer's existing from-import when already loaded."""
    modules = [format, legacy_format]
    upload = sys.modules.get("reflex_components_core.core._upload")
    if upload is not None:
        modules.append(upload)
    return modules


@contextlib.contextmanager
def backend(name):
    """Select the native mojson socket path or the untouched PR baseline."""
    original = format.orjson
    sockets = [(module, module.orjson_dumps_socket) for module in _socket_modules()]
    format.orjson = {"orjson": orjson, "mojson": MOJSON_PROXY, "stdlib": None}[name]
    for module, _ in sockets:
        module.orjson_dumps_socket = _socket if name == "mojson" else _original_socket
    try:
        yield
    finally:
        format.orjson = original
        for module, original_socket in sockets:
            module.orjson_dumps_socket = original_socket


def install():
    """Select mojson in a downstream app or pytest session."""
    format.orjson = MOJSON_PROXY
    for module in _socket_modules():
        module.orjson_dumps_socket = _socket
