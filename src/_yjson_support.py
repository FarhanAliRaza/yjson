"""Cold type conversion and strict stdlib-backed decoding for yjson."""
import dataclasses
import datetime
import enum
import json
import math
import sys
import uuid

OPT_INDENT_2 = 1
OPT_NAIVE_UTC = 2
OPT_NON_STR_KEYS = 4
OPT_OMIT_MICROSECONDS = 8
OPT_SERIALIZE_NUMPY = 16
OPT_SORT_KEYS = 32
OPT_STRICT_INTEGER = 64
OPT_UTC_Z = 128
OPT_PASSTHROUGH_SUBCLASS = 256
OPT_PASSTHROUGH_DATETIME = 512
OPT_APPEND_NEWLINE = 1024
OPT_PASSTHROUGH_DATACLASS = 2048
OPT_SERIALIZE_DATACLASS = 0
OPT_SERIALIZE_UUID = 0
JSONEncodeError = TypeError


class JSONDecodeError(json.JSONDecodeError):
    __module__ = "yjson"


class Fragment:
    """Insert pre-serialized JSON verbatim. Contents are deliberately unvalidated."""
    __slots__ = ("_data",)
    __module__ = "yjson"

    def __init__(self, value):
        if type(value) is str:
            try:
                value = value.encode("utf-8")
            except UnicodeEncodeError:
                pass  # lone surrogates: dumps() raises JSONEncodeError, as orjson does
        elif type(value) is not bytes:
            raise TypeError("Fragment requires bytes or str")
        self._data = value


class _DataclassFields(dict):
    pass


def _utc_offset(obj):
    """orjson's rule: pendulum (convert) through the datetime, pytz (normalize) after normalizing, else utcoffset(dt)."""
    tzinfo = obj.tzinfo
    if tzinfo is None:
        return None
    if isinstance(tzinfo, datetime.timezone) or hasattr(tzinfo, "convert"):
        return obj.utcoffset()
    if hasattr(tzinfo, "normalize"):
        return tzinfo.normalize(obj).utcoffset()
    if hasattr(tzinfo, "dst"):
        return tzinfo.utcoffset(obj)
    raise TypeError("datetime's timezone library is not supported: use datetime.timezone.utc, pendulum, pytz, or dateutil")


def _datetime_string(obj, option):
    if isinstance(obj, datetime.datetime):
        offset = _utc_offset(obj)
        if offset is None and option & OPT_NAIVE_UTC:
            offset = datetime.timedelta(0)
        text = obj.replace(tzinfo=None).isoformat(timespec="seconds" if option & OPT_OMIT_MICROSECONDS or not obj.microsecond else "microseconds")
        if offset is not None:
            if not offset and option & OPT_UTC_Z:
                return text + "Z"
            seconds = offset.days * 86400 + offset.seconds
            sign = "-" if seconds < 0 else "+"
            minutes = (abs(seconds) + 30) // 60
            return text + f"{sign}{minutes // 60:02d}:{minutes % 60:02d}"
    elif isinstance(obj, datetime.time):
        if obj.tzinfo is not None:
            raise TypeError("datetime.time must not have tzinfo set")
        text = obj.isoformat(timespec="seconds" if option & OPT_OMIT_MICROSECONDS or not obj.microsecond else "microseconds")
    else:
        text = obj.isoformat()
    return text


def convert(obj, default, option):
    if isinstance(obj, enum.Enum):
        return obj.value
    if isinstance(obj, (datetime.datetime, datetime.date, datetime.time)):
        if not option & OPT_PASSTHROUGH_DATETIME:
            return _datetime_string(obj, option)
    elif type(obj) is uuid.UUID:
        return str(obj)
    elif dataclasses.is_dataclass(obj) and not isinstance(obj, type):
        if not option & OPT_PASSTHROUGH_DATACLASS:
            return _DataclassFields((field.name, getattr(obj, field.name)) for field in dataclasses.fields(obj) if not field.name.startswith("_"))
    elif not option & OPT_PASSTHROUGH_SUBCLASS:
        if isinstance(obj, str):
            return str.__str__(obj)
        if isinstance(obj, int):
            return int.__int__(obj)
        if isinstance(obj, list):
            return list(list.__iter__(obj))
        if isinstance(obj, dict):
            return dict(dict.items(obj))
    if default is not None:
        try:
            return default(obj)
        except Exception as error:
            raise TypeError(f"Type is not JSON serializable: {type(obj).__name__}") from error
    raise TypeError(f"Type is not JSON serializable: {type(obj).__name__}")


def _key_string(key, option, native):
    if isinstance(key, str):
        return str.__str__(key)
    if not option & OPT_NON_STR_KEYS:
        raise TypeError("Dict key must be str")
    if isinstance(key, enum.Enum):
        return _key_string(key.value, option, native)
    if key is None:
        return "null"
    if type(key) is bool:
        return "true" if key else "false"
    if isinstance(key, int):
        value = int.__int__(key)
        if not option & 65536 and not -(1 << 63) <= value < (1 << 64):
            raise TypeError("Integer exceeds 64-bit range")
        return str(value)
    if type(key) is float:
        if option & 65536 and not math.isfinite(key):
            return "NaN" if math.isnan(key) else ("Infinity" if key > 0 else "-Infinity")
        return native(key).decode("utf-8") if math.isfinite(key) else "null"
    if isinstance(key, (datetime.datetime, datetime.date, datetime.time)):
        return _datetime_string(key, option)
    if type(key) is uuid.UUID:
        return str(key)
    raise TypeError("Dict key must a type serializable with OPT_NON_STR_KEYS")


def _float(text):
    result = float(text)
    if not math.isfinite(result):
        raise ValueError("number is infinity when parsed as double")
    return result


def _integer(text):
    result = int(text)
    return result if -(1 << 63) <= result < (1 << 64) else _float(text)


def _constant(text):
    raise ValueError(f"unexpected character: {text}")


def _parse(text):
    try:
        return json.loads(text, parse_constant=_constant, parse_float=_float, parse_int=_integer)
    except RecursionError:
        # CPython < 3.12 charges the C scanner's nesting to the Python recursion limit (1000 by
        # default), which documents at orjson's 1024-deep limit exceed. Retry with room for them;
        # anything deeper still fails and is reported as a decode error by the caller.
        limit = sys.getrecursionlimit()
        sys.setrecursionlimit(limit + 1100)
        try:
            return json.loads(text, parse_constant=_constant, parse_float=_float, parse_int=_integer)
        finally:
            sys.setrecursionlimit(limit)


def loads(obj, /):
    if type(obj) not in (str, bytes, bytearray, memoryview):
        raise JSONDecodeError("Input must be bytes, bytearray, memoryview, or str", "", 0)
    try:
        if type(obj) is memoryview:
            if not obj.c_contiguous:
                raise ValueError("Input memoryview must be contiguous")
            obj = obj.tobytes()
        text = obj if type(obj) is str else obj.decode("utf-8")
        text.encode("utf-8")  # reject lone surrogate codepoints in input
        result = _parse(text)
        # json.loads accepts escaped lone surrogates; RFC 8259 UTF-8 output does not.
        # Containers nest at most 1024 deep (orjson's limit), counted from the top-level one.
        pending = [(result, 0)]
        while pending:
            value, depth = pending.pop()
            if type(value) is str:
                value.encode("utf-8")
            elif depth >= 1024 and type(value) in (dict, list):
                raise ValueError("array and object recursion depth exceeded")
            elif type(value) is dict:
                pending.extend((key, depth + 1) for key in value)
                pending.extend((child, depth + 1) for child in value.values())
            elif type(value) is list:
                pending.extend((child, depth + 1) for child in value)
        return result
    except json.JSONDecodeError as error:
        raise JSONDecodeError(error.msg, error.doc, error.pos) from None
    except (ValueError, UnicodeError, RecursionError) as error:
        raise JSONDecodeError(str(error), locals().get("text", ""), 0) from None


loads.__module__ = "yjson"
