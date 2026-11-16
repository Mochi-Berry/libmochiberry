---
sidebar_position: 2
---

# Building

mbjs uses [CMake] as its main build system, with an additional helper [Makefile].

:::note
Windows users will need to run the CMake commands manually.
:::

## Getting the source

```bash
git clone https://github.com/gryzzlaycutzy121/libmochiberry.git
cd libmochiberry
```

## Compiling everything

```bash
make
```

This will build the `mbjs` and `mbjsc` executables and other test tools. Head over [here](./cli) for
instructions on how to use them.

## Amalgamated builds

The amalgamated build rolls individual source files into a single big file.
It's a good choice for projects that want to use mbjs without CMake.

Download mbjs-amalgam.zip from [Releases](https://github.com/gryzzlaycutzy121/libmochiberry/releases)

To enable the std, os and bjson modules, compile mbjs-amalgam.c with
`-DMBJS_BUILD_LIBC`.

## Debug builds

```bash
make debug
```

This will produce a debug build without optimizations, suitable for developers.

## Running test262

```bash
make test262
```

This will run the test262 suite.

```bash
make test262-update
```

This will run the test262 suite and update the error / pass report, useful after
implementing a new feature that would alter the result of the test suite.

[CMake]: https://cmake.org
[Makefile]: https://www.gnu.org/software/make/
