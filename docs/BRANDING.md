# PanPal branding

## Public product name

**PanPal** is the public product and project name.

The GitHub repository slug is `panpal-cardputer`; the supported hardware
target is currently the M5Stack Cardputer ADV.

Suggested description:

> PanPal is a personal voice and coding companion that turns the M5Stack
> Cardputer ADV into a wireless keyboard, microphone, animated pet, and live
> task-status display for Windows and macOS.

## Compatibility names kept intentionally

The following names remain technical compatibility identifiers for the current
1.x line and must not be renamed casually:

- `CardBridge.app` and `CardBridgeAgent.app` bundle paths;
- the `cardbridge` Python package and CLI;
- `_cardbridge._tcp`, ports, local socket paths, and bundle identifiers;
- `CardBridge Microphone` and `CardBridge Microphone Feed` Core Audio devices;
- existing `~/.cardbridge` configuration and Keychain records;
- the Windows `CardBridge.exe` filename, `CodexDeck` configuration/log
  directories, installer `AppId`, and single-instance mutex;
- release artifact names beginning with `CardBridge-`.

This lets the product present as PanPal without forcing existing users to
re-pair devices, re-authorize Accessibility, or recreate audio settings. A
future major release may migrate these identifiers with an explicit upgrade
plan.

PanPal is independent and is not an official OpenAI product or endorsed by
OpenAI.
