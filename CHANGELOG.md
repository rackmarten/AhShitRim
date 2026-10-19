# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- README: concept, architecture, planned features, milestone 1, requirements and asset policy.
- Scaffold: SKSE plugin (`skse/`, CommonLibSSE-NG v10.1.0, CMake + vcpkg) and 32-bit San Andreas ASI plugin (`sa/`, plugin-sdk, CMake with the Visual Studio generator). Both log when they load; the ASI refuses to start on any exe but GTA: San Andreas 1.0 US and logs why.
- Protocol v1 link ([docs/PROTOCOL.md](docs/PROTOCOL.md)): a shared memory section with magic, protocol version and a heartbeat per side. Each side logs when the other appears, disappears or speaks another version. The same link code compiles into both plugins (64-bit and 32-bit).
- Game-free unit tests for the protocol header layout and the handshake (doctest, `scripts/test.ps1`).
- `scripts/build.ps1`, `deploy-local.ps1`, `test.ps1`, `logs.ps1`; `local.props.example` for game paths.
- [docs/DESIGN.md](docs/DESIGN.md), [docs/PROTOCOL.md](docs/PROTOCOL.md), [docs/DEV_SETUP.md](docs/DEV_SETUP.md) (Build Tools, plugin-sdk, the San Andreas downgrade and ASI loader).
