import argparse
import json
from pathlib import Path

from local_env import wifi_config


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    ssid, password, host = wifi_config(args.env)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "\n".join(
            [
                "#pragma once",
                f"#define PB_WIFI_SSID {json.dumps(ssid)}",
                f"#define PB_WIFI_PASSWORD {json.dumps(password)}",
                f"#define PB_LOG_SERVER_IP {json.dumps(host)}",
                "#define PB_LOG_SERVER_PORT 8080",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print("Generated local Wi-Fi configuration (credentials hidden).")


if __name__ == "__main__":
    main()
