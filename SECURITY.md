# Security Policy

## Supported versions

This project is pre-1.0 (beta). Security fixes target the latest release and
`main`.

## Reporting a vulnerability

Please report security issues **privately** — do not open a public issue.

- Use GitHub's [private vulnerability reporting](https://github.com/glucoscout/esp32-cgm/security/advisories/new), or
- email **security@glucoscout.com**.

Include reproduction steps and affected version/commit. We aim to acknowledge
within a few business days and will coordinate a fix and disclosure timeline
with you.

## A note on credentials

This firmware keeps Wi-Fi and Nightscout secrets in a **git-ignored**
`secrets.h` (never commit it — use the provided `secrets.example.h`). If you
believe a credential was committed to this repository, report it via the
channels above so we can rotate and purge it.
