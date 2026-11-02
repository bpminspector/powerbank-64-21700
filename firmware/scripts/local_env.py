import ipaddress
import shlex
from pathlib import Path


def read_env(path: Path) -> dict[str, str]:
    values = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped.startswith("export "):
            stripped = stripped[7:].lstrip()
        if "=" not in stripped:
            raise ValueError(f"{path}:{line_number}: expected KEY=value")
        key, raw_value = stripped.split("=", 1)
        try:
            parsed = shlex.split(raw_value, comments=True, posix=True)
        except ValueError as exc:
            raise ValueError(f"{path}:{line_number}: invalid value syntax") from exc
        values[key.strip()] = parsed[0] if parsed else ""
    return values


def wifi_config(path: Path) -> tuple[str, str, str]:
    values = read_env(path)
    ssid = values.get("WIFI_SSID") or values.get("WIFI_RSID", "")
    password = values.get("WIFI_PASSWORD", "")
    host = values.get("WIFI_LOCAL_ADMIN_SERVER_IP", "")

    if not ssid or len(ssid.encode("utf-8")) > 32:
        raise ValueError("WIFI_RSID/WIFI_SSID must contain 1 to 32 UTF-8 bytes")
    if not password or len(password.encode("utf-8")) > 63:
        raise ValueError("WIFI_PASSWORD must contain 1 to 63 UTF-8 bytes")
    try:
        ipaddress.IPv4Address(host)
    except ipaddress.AddressValueError as exc:
        raise ValueError("WIFI_LOCAL_ADMIN_SERVER_IP must be a valid IPv4 address") from exc

    return ssid, password, host
