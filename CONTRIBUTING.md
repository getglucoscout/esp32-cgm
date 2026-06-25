# Contributing

Thanks for your interest in improving esp32-cgm. Contributions of all kinds —
bug reports, board support, fixes, and docs — are welcome.

## Ground rules

- **Never commit secrets.** Wi-Fi/Nightscout credentials belong in the
  git-ignored `secrets.h`. Only ever commit `secrets.example.h` with
  placeholder values. CI scans every change for leaked credentials.
- Keep board-specific code under `boards/<chip>/<vendor-board>/` and put
  portable logic in `common/` so it can be shared across boards.
- This is not a medical device; don't add claims or features that imply medical
  use (see [DISCLAIMER.md](DISCLAIMER.md)).

## Workflow

1. Fork and create a branch off `main`.
2. Make your change. If you add a board, follow the existing layout and include
   a short board README.
3. Make sure CI passes (secret scan + build).
4. Open a pull request describing what changed and why, and which hardware you
   tested on.

## Adding a new board

Create `boards/<chip>/<vendor-board>/` with the firmware sources, a
`secrets.example.h`, a `version.h`, and a README covering wiring, the display
driver, and how to flash. Update the supported-hardware table in the main
README.

## Versioning

We use [Semantic Versioning](https://semver.org/) and
[Keep a Changelog](https://keepachangelog.com/). Add a note to the
`[Unreleased]` section of `CHANGELOG.md` with your change.

## Reporting bugs

Use the issue templates. Include your board, firmware version (from
`version.h` / the Releases page), and steps to reproduce.
