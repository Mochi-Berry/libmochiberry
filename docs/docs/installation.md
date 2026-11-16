---
sidebar_position: 3
---

# Installation

Installing mbjs is simple, and we provide several ways to do so.


## Build from source

If you built it from source as outlined in [building](./building) you can just run:

```bash
make install
```

and it will be installed in your system. The default installation path is `/usr/local`.


## Using a prebuilt binary

Each [release on GitHub] includes binaries for several systems and architectures.


## Using jsvu

As of version 2.2.0 of `jsvu`, mbjs-ng will be installed when the `mbjs` engine is requested.

```bash
npm install jsvu -g
```

[release on GitHub]: https://github.com/gryzzlaycutzy121/libmochiberry/releases
