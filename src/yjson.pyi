from typing import Any, Callable
from json import JSONDecodeError as _JSONDecodeError

JSONEncodeError = TypeError
class JSONDecodeError(_JSONDecodeError): ...
class Fragment:
    def __init__(self, value: bytes | str) -> None: ...

def dumps(obj: Any, /, default: Callable[[Any], Any] | None = ..., option: int | None = ...) -> bytes: ...
def dumps_socket(
    obj: Any,
    /,
    default: Callable[[Any], Any] | None = ...,
    classify: Callable[[type], tuple[str, ...] | Callable[[Any], Any] | None] | None = ...,
) -> bytes:
    """Compact JSON with Python json semantics: big integers, NaN/Infinity, escaped lone surrogates.

    classify(type) is asked once per call for each type that would go to default: a tuple of
    attribute names writes the object as that dict, a callable writes its result, None uses default.
    """
    ...
def loads(obj: str | bytes | bytearray | memoryview, /) -> Any: ...

OPT_INDENT_2: int
OPT_NAIVE_UTC: int
OPT_NON_STR_KEYS: int
OPT_OMIT_MICROSECONDS: int
OPT_SERIALIZE_NUMPY: int
OPT_SORT_KEYS: int
OPT_STRICT_INTEGER: int
OPT_UTC_Z: int
OPT_PASSTHROUGH_SUBCLASS: int
OPT_PASSTHROUGH_DATETIME: int
OPT_APPEND_NEWLINE: int
OPT_PASSTHROUGH_DATACLASS: int
OPT_SERIALIZE_DATACLASS: int
OPT_SERIALIZE_UUID: int
