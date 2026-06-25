## What does this PR do?

Brief description of the change.

## Hardware tested

- Board / chip:
- Firmware version:

## Checklist

- [ ] No secrets committed (real values stay in the git-ignored `secrets.h`)
- [ ] Board-specific code is under `boards/<chip>/<vendor-board>/`; shared logic in `common/`
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
- [ ] CI passes (secret scan + build)
