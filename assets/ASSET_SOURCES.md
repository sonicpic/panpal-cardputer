# Asset sources and permissions

This inventory exists so a public release does not accidentally redistribute
an asset without permission.

| Asset | Source / status |
| --- | --- |
| `assets/fonts/cardbridge-ui-13.bff` | Generated from Source Han Sans CN Medium; see `assets/fonts/LICENSE-SourceHanSans.txt`. |
| `macos/App/AppIcon-1024.png` | CardBridge project artwork; confirm copyright ownership before accepting outside contributions. |
| `docs/codex-dialog-pixel-background*.png` and `docs/codex_rpg_layout.svg` | Product/UI reference artwork; confirm whether generated or commissioned and record the source license before redistribution. |
| `src/pet_assets.*` | Generated animation payload; record the source atlas/manifest and permission for any public pet artwork used to regenerate it. |
| `src/ui_background_asset.*` | Generated from the checked-in UI background source; record source ownership and regeneration command. |

Generated binary files should remain reproducible from documented source
inputs. If an asset is not owned by the project, replace it with a licensed
alternative or add its exact attribution and redistribution permission before
publishing a release.
