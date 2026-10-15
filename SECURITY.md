# Security

## Reporting a vulnerability

Please do not open a public issue. Use **Report a vulnerability** in the
[Security tab](https://github.com/nic0lasm/zeus-poseidon-arm64/security/advisories/new) of this
repository; the report stays private until a fix is released.

Include the version (or commit), your Mac model and macOS version, and the steps or the file
that trigger the problem. You will get an answer within a week.

## Scope

The application reads files the user provides (the GOG installer, CD images, saved games and
adventures) and runs the game's x86 code inside an emulator. Reports are especially welcome
when:

- emulated code can read or write host memory or files outside the write directory;
- a crafted installer, CD image, saved game or map makes a tool or the application crash in a
  way that looks exploitable;
- a release archive contains something it should not.

A game that stops on a Windows function the host does not provide yet is not a security issue:
open a normal issue for it.

## Supported versions

Only the latest release receives fixes.
