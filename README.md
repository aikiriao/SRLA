![eclint](https://github.com/aikiriao/SRLA/workflows/eclint/badge.svg?branch=main)
![C/C++ CI](https://github.com/aikiriao/SRLA/workflows/C/C++%20CI/badge.svg?branch=main)
![Repo size](https://img.shields.io/github/repo-size/aikiriao/SRLA)
![License](https://img.shields.io/github/license/aikiriao/SRLA)

# SRLA

aka Soleil Rising Lossless Audio codec

# How to build

## Requirement

* [CMake](https://cmake.org) >= 3.15

## Build SRLA Codec

```bash
git clone https://github.com/aikiriao/SRLA.git
cd SRLA/tools/srla_codec
cmake -B build
cmake --build build
```

# Usage

## SRLA Codec

### Encode

```bash
./srla -e INPUT.wav OUTPUT.srl
```

you can change compression mode by `-m` option.
Following example encoding in maximum compression (but slow) option.

```bash
./srla -e -m 5 INPUT.wav OUTPUT.srl
```

### Decode

```bash
./srla -d INPUT.srl OUTPUT.wav
```

## License

MIT
