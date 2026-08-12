# Taser

Taser is a TURN/STUN server written in modern C++ (C++20), built on
Boost.Asio coroutines. It implements the core STUN (RFC 5389/8489) and
TURN (RFC 5766/8656) protocols, with transport demultiplexing per
RFC 7983.

Taser depends on [`libstunxx`](https://github.com/Cmoney12/libstunxx),
a zero-dependency STUN/TURN message codec library.

> **Status:** actively in development. UDP transport is currently
> implemented and functional; TCP/TLS TURN allocations (RFC 6062) and

## Features

- STUN Binding requests/responses (RFC 5389/8489)
- TURN allocation, permission, and channel-binding handling over UDP
  (RFC 5766/8656)
- Long-term credential authentication
- Async I/O via Boost.Asio coroutines (C++20 `co_await`)
- Structured logging via spdlog (optional at build time)

## Requirements

- A C++20-capable compiler (GCC 12+ / Clang 15+ recommended)
- CMake 3.20+
- Boost (Asio) development libraries
- OpenSSL development libraries
- spdlog (optional; enabled via CMake option)
- GoogleTest (fetched automatically for the test suite, or provided
  system-wide)

## Building

Clone recursively so the `libstunxx` submodule is pulled in:

```bash
git clone --recursive https://github.com/Cmoney12/Taser.git
cd Taser
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

Configure and build with CMake:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The server binary will be produced under `build/`.

## Running

Start the server (defaults shown; adjust flags/config as needed for
your setup):

```bash
./build/taser
```

## Testing it with `turnutils`

The [`coturn`](https://github.com/coturn/coturn) project's `turnutils`
tools are useful for exercising the server manually. All examples
below assume the server is listening on `127.0.0.1` with a
long-term-credential user `alice` / `secret1`.

Start a peer to relay traffic to/from:

```bash
turnutils_peer -v -p 3480
# or, using the default port:
turnutils_peer -v
```

Run a basic UDP client session:

```bash
turnutils_uclient -v -l 1000 -u alice -w secret1 -c -e 127.0.0.1 127.0.0.1
```

Send indications instead of regular data (`-y`):

```bash
turnutils_uclient -v -l 1000 -y -u alice -w secret1 -c -e 127.0.0.1 127.0.0.1
```

Test with SEND/DATA (`-s`):

```bash
turnutils_uclient -v -l 1000 -u alice -w secret1 -s -e 127.0.0.1 127.0.0.1
```

> Note: `turnutils_peer` must be running and listening on the port
> your client's `-e` peer address targets (default `3480`), or the
> relay will silently see no return traffic.

## Running the test suite

```bash
cmake --build build --target test
# or
ctest --test-dir build
```

## Project layout

```
taser/
├── libstunxx/       # STUN/TURN codec library (git submodule)
├── src/             # Server implementation
├── include/taser/   # Public headers
├── test/            # GoogleTest unit/integration tests
└── CMakeLists.txt
```

## Roadmap

- [x] STUN Binding
- [x] TURN UDP allocations, permissions, channel binding
- [ ] TURN over TCP (RFC 6062)
- [ ] TLS/DTLS transports

## License

This project is licensed under the [MIT License](LICENSE).

