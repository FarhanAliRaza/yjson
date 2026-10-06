# SPDX-License-Identifier: (Apache-2.0 OR MIT)
# Copyright Anders Kaseorg (2023)

import yjson


class C:
    c: "C"

    def __del__(self):
        yjson.loads('"' + "a" * 10000 + '"')


def test_reentrant():
    c = C()
    c.c = c
    del c

    yjson.loads("[" + "[]," * 1000 + "[]]")
