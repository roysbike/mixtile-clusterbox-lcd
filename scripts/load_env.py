# PlatformIO pre-script. Reads .env and passes credentials as string macros.
Import("env")

from pathlib import Path

keys = (
    "WIFI_SSID",
    "WIFI_PASSWORD",
    "CLUSTER_HOST",
    "CLUSTER_USER",
    "CLUSTER_PASSWORD",
)
required = ("CLUSTER_HOST", "CLUSTER_USER", "CLUSTER_PASSWORD")

path = Path(env["PROJECT_DIR"]) / ".env"
if not path.is_file():
    print("error: copy .env.example to .env and fill in the BMC login")
    env.Exit(1)

values = {key: "" for key in keys}
for raw in path.read_text().splitlines():
    line = raw.strip()
    if not line or line.startswith("#") or "=" not in line:
        continue
    key, value = line.split("=", 1)
    key = key.strip()
    if key not in values:
        continue
    values[key] = value.strip().strip('"').strip("'")

missing = [key for key in required if not values[key]]
if missing:
    print("error: .env is missing " + ", ".join(missing))
    env.Exit(1)

env.Append(CPPDEFINES=[(key, '\\"%s\\"' % values[key].replace("\\", "\\\\").replace('"', '\\"')) for key in keys])
