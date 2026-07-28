## Summary

Describe the user-visible change and why it is needed.

## Permissions, privacy, and compatibility

- Does this change Windows permissions, pairing, tokens, Codex data handling,
  local ports, protocol versions, or update behavior?
- Is an upgrade or migration required?

## Validation

- [ ] `powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1`
- [ ] `git diff --check`
- [ ] Hardware validation documented if firmware behavior changed
- [ ] Release notes and version compatibility updated when required
