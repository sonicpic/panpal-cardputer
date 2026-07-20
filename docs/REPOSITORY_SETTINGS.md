# GitHub repository settings checklist

These settings live in the GitHub repository rather than the checkout. Apply
them before switching the project from private development to a public release:

- Enable private vulnerability reporting and confirm the Security Advisory
  contact described in `SECURITY.md`.
- Enable secret scanning and push protection where the repository plan allows.
- Enable Dependabot alerts and security updates.
- Protect the default branch: required CI, no force-push, and at least one
  review for changes to permissions, protocol, driver, or release workflows.
- Require signed tags for public releases and restrict who can publish a
  release.
- Store Developer ID, notarization, and Sparkle private-key material only as
  GitHub Actions secrets or macOS Keychain records. Never commit them.
- Make the first public release a draft, verify the DMG, ZIP, firmware hash,
  SBOM, notices, and appcast manually, then publish it.
