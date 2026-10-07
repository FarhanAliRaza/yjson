# Third-party notices

These notices apply to the derived portions identified below.

## Żmij

The floating-point core, digit conversion and power-of-ten table in
`src/yjson.mojo` and `tools/` are ported from
[Żmij](https://github.com/vitaut/zmij).
[Upstream license](https://github.com/vitaut/zmij/blob/main/LICENSE):

```text
MIT License

Copyright (c) 2025 Victor Zverovich

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## itoap

The integer writer in `src/yjson.mojo` is ported from
[itoap](https://github.com/Kogia-sima/itoap), version 1.0.1.
The notice below is preserved from that crate.

```text
The MIT License (MIT)
 Copyright (c) 2014-2016 Milo Yip, 2020 Ryohei Machida

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 OR OTHER DEALINGS IN THE SOFTWARE.
```

## fast_float

The float parsers in `src/decoder.mojo` and `bench/c_decoder/decoder.c` (`eisel_lemire` and the eight-digit routines) and the
power-of-five table generator `tools/powers_of_five.py` follow
[fast_float](https://github.com/fastfloat/fast_float), which is licensed under
the Apache License 2.0, the MIT License or the Boost Software License 1.0 at the
user's option; the MIT License is reproduced here.
[Upstream license](https://github.com/fastfloat/fast_float/blob/main/LICENSE-MIT):

```text
MIT License

Copyright (c) 2021 The fast_float authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## orjson test suite

The tests in `tests/suite/` and their fixtures in `tests/suite/data/` are copied
from [orjson](https://github.com/ijl/orjson) 3.12.0 (tag `3.12.0`, commit
`6737895`), with the module renamed so they exercise yjson. Each test file keeps
its upstream copyright line and `SPDX-License-Identifier` header, which state its
license: `(Apache-2.0 OR MIT)` or `MPL-2.0`. The upstream license texts are
[LICENSE-APACHE](https://github.com/ijl/orjson/blob/3.12.0/LICENSE-APACHE),
[LICENSE-MIT](https://github.com/ijl/orjson/blob/3.12.0/LICENSE-MIT) and
[LICENSE-MPL-2.0](https://github.com/ijl/orjson/blob/3.12.0/LICENSE-MPL-2.0).
The MPL-2.0 files are distributed under the MPL-2.0; their source is this
repository.
