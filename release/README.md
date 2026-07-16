# CardBridge 发布

`compatibility.json` 由根目录 `version.json` 生成；`appcast.xml` 是 Sparkle 稳定更新源。

本机候选构建：

```sh
CODE_SIGN_IDENTITY="Apple Development: …" macos/scripts/release.sh
```

公开发布必须使用 `Developer ID Application`，并先把 Notary 凭据保存到 Keychain：

```sh
xcrun notarytool store-credentials cardbridge-notary
REQUIRE_NOTARIZATION=1 \
NOTARY_PROFILE=cardbridge-notary \
CODE_SIGN_IDENTITY="Developer ID Application: …" \
macos/scripts/release.sh
```

Sparkle 私钥只保存在本机 Keychain，账户名固定为 `com.voltwake.cardbridge`；仓库仅保存公钥。
