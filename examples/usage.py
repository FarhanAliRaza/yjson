import math
import numpy as np
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build"))
import mojson

# 1. Basic use: returns bytes, compact, UTF-8 (like orjson.dumps)
data = {"id": 1, "name": "Ada", "tags": ["x", "y"], "score": 0.1, "ok": True, "none": None}
out = mojson.dumps(data)
print(out)                         # b'{"id":1,...}'
print(out.decode())                # need a str? decode it

# 2. NaN / Infinity: std-json tokens (orjson would write null)
print(mojson.dumps([math.nan, math.inf, -math.inf]))

# 3. NumPy: arrays and scalars, no .tolist() needed
arr = np.array([[1.5, np.nan], [3.0, -np.inf]])
print(mojson.dumps({"matrix": arr, "mean": np.float64(2.25), "n": np.int64(4)}))

# 4. Writing to a file
with open("out.json", "wb") as f:
    f.write(mojson.dumps(data))

# 5. Errors: same cases as orjson / json
for bad in [{1: "int key"}, {"x": object()}]:
    try:
        mojson.dumps(bad)
    except mojson.JSONEncodeError as e:
        print(type(e).__name__, e)

# 6. Common Python types and custom conversions
from dataclasses import dataclass
from datetime import datetime
from decimal import Decimal
from uuid import UUID

@dataclass
class Event:
    id: UUID
    created: datetime

event = Event(UUID(int=1), datetime(2024, 1, 2))
print(mojson.dumps(event, option=mojson.OPT_NAIVE_UTC | mojson.OPT_UTC_Z))
print(mojson.dumps({"amount": Decimal("12.50")}, default=str))

# 7. Formatting, non-string keys, and decoding
print(mojson.dumps({2: "b", 1: "a"}, option=mojson.OPT_NON_STR_KEYS | mojson.OPT_SORT_KEYS | mojson.OPT_INDENT_2))
print(mojson.loads(mojson.dumps(data)))
print(mojson.dumps({"cached": mojson.Fragment(b'{"already":"serialized"}')}))
