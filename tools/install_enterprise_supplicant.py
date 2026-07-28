"""Install the pinned enterprise-compatible WPA supplicant archive.

PlatformIO executes this before compiling the Arduino framework. The package
cache is project-local, so copying this archive cannot alter sibling projects.
"""

from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


EXPECTED_PACKAGE_VERSION = "5.4.0+sha.858a988d6e"
EXPECTED_ARCHIVE_SHA256 = (
    "9c0cdc624d199f84678bb3dbdbfa3fecba21b42a4350f5d1325d8a19b28a7842"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


project_dir = Path(env.subst("$PROJECT_DIR")).resolve()  # type: ignore[name-defined]
source = project_dir / "firmware" / "vendor" / "esp32s3" / "libwpa_supplicant.a"
if not source.is_file():
    raise RuntimeError(f"Missing enterprise supplicant archive: {source}")

source_hash = sha256(source)
if source_hash != EXPECTED_ARCHIVE_SHA256:
    raise RuntimeError(
        "Enterprise supplicant archive checksum mismatch: "
        f"expected {EXPECTED_ARCHIVE_SHA256}, got {source_hash}"
    )

package_dir = Path(
    env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")  # type: ignore[name-defined]
).resolve()
manifest_path = package_dir / "package.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
actual_version = str(manifest.get("version", ""))
if actual_version != EXPECTED_PACKAGE_VERSION:
    raise RuntimeError(
        "Enterprise supplicant archive does not match installed IDF libraries: "
        f"expected {EXPECTED_PACKAGE_VERSION}, got {actual_version or 'unknown'}"
    )

destination = package_dir / "esp32s3" / "lib" / "libwpa_supplicant.a"
if not destination.is_file() or sha256(destination) != source_hash:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)

print(
    "*** Enterprise Wi-Fi supplicant: compatibility TLS 1.0-1.2 "
    f"({source_hash[:12]}) ***"
)
