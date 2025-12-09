Import("env")
import os
import shutil
import tempfile
from contextlib import contextmanager
from pathlib import Path
from SCons.Script import COMMAND_LINE_TARGETS

APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"
LITTLEFS_BIN = "$BUILD_DIR/littlefs.bin"
MERGED_BIN = "$BUILD_DIR/${PROGNAME}_merged.bin"
BOARD_CONFIG = env.BoardConfig()
DEFAULT_LFS_ADDRESS = "0x3d0000"
SECRET_FILENAMES = ("secrets.json",)
IS_UPLOADFS_TARGET = "uploadfs" in COMMAND_LINE_TARGETS


@contextmanager
def _temporarily_hide_paths(paths):
    backups = []
    staging_dir = None
    try:
        for path in paths:
            if path.exists():
                if staging_dir is None:
                    staging_dir = Path(tempfile.mkdtemp(prefix="fs_secrets_"))
                backup = staging_dir / path.name
                shutil.move(str(path), backup)
                backups.append((path, backup))
        if backups:
            print("Excluding secret files from LittleFS image.")
        yield
    finally:
        for original, backup in reversed(backups):
            if backup.exists():
                shutil.move(str(backup), str(original))
        if staging_dir and staging_dir.exists():
            shutil.rmtree(staging_dir, ignore_errors=True)


def _format_command(parts):
    """Join command arguments while keeping paths with spaces quoted."""
    formatted = []
    for part in parts:
        if not isinstance(part, str):
            part = str(part)
        if " " in part and not part.startswith('"'):
            formatted.append(f'"{part}"')
        else:
            formatted.append(part)
    return " ".join(formatted)


def _discover_partition_csv(env_obj):
    """Locate the partition CSV for the active board."""
    partitions_csv = None

    if hasattr(env_obj, "GetProjectOption"):
        partitions_csv = env_obj.GetProjectOption("board_build.partitions", None)
        if partitions_csv:
            candidate = Path(env_obj.subst(partitions_csv))
            if candidate.is_file():
                return candidate

    framework_dir = Path(env_obj.subst("$PROJECT_PACKAGES_DIR")) / "framework-arduinoespressif32"
    board_name = env_obj.subst("$BOARD")
    variants_dir = framework_dir / "variants" / board_name

    if variants_dir.is_dir():
        csv_files = sorted(variants_dir.glob("*.csv"))
        if csv_files:
            return csv_files[0]

    flash_size = BOARD_CONFIG.get("upload.flash_size", "4MB")
    default_csv_name = "default_8MB.csv" if flash_size == "8MB" else "default.csv"
    default_csv = framework_dir / "tools" / "partitions" / default_csv_name
    if default_csv.is_file():
        return default_csv

    generated = Path(env_obj.subst("$BUILD_DIR")) / "partitions.csv"
    if generated.is_file():
        return generated

    return None


def get_littlefs_partition_address(env_obj):
    """Return the LittleFS offset from the partition CSV or a safe default."""
    csv_path = _discover_partition_csv(env_obj)
    if not csv_path:
        print("Warning: partition CSV not found, using default LittleFS address 0x3d0000")
        return DEFAULT_LFS_ADDRESS

    try:
        with csv_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = [segment.strip() for segment in line.split(",")]
                if len(parts) < 5:
                    continue
                name, type_field, subtype, offset, _size = parts[:5]
                matches_name = "spiffs" in name.lower() or "littlefs" in name.lower()
                matches_type = type_field.lower() == "data" and subtype.lower() in ("spiffs", "littlefs")
                if matches_name or matches_type:
                    offset = offset.strip("\"'")
                    if not offset.startswith("0x"):
                        offset = f"0x{offset}"
                    print(f"LittleFS partition '{name}' found at {offset}")
                    return offset
    except Exception as exc:
        print(f"Warning: failed to read {csv_path}: {exc}")

    print("Warning: LittleFS partition not found, using default address 0x3d0000")
    return DEFAULT_LFS_ADDRESS


def build_littlefs(source, target, env, **_kwargs):
    """Ensure the filesystem image matches the most recent UI assets."""
    data_dir = Path(env.subst("$PROJECTDATA_DIR"))
    secret_paths = [data_dir / name for name in SECRET_FILENAMES]
    cmd = [
        env.subst("$PYTHONEXE"),
        "-m",
        "platformio",
        "run",
        "-e",
        env.subst("$PIOENV"),
        "--target",
        "buildfs",
        "--disable-auto-clean",
    ]
    with _temporarily_hide_paths(secret_paths):
        env.Execute(_format_command(cmd))


def merge_bin(source, target, env, **_kwargs):
    littlefs_address = get_littlefs_partition_address(env)
    littlefs_path = env.subst(LITTLEFS_BIN)
    if not os.path.exists(littlefs_path):
        print(f"Warning: {littlefs_path} not found, skipping merged image generation")
        return

    flash_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", []))
    flash_images += ["$ESP32_APP_OFFSET", APP_BIN, littlefs_address, LITTLEFS_BIN]

    resolved_images = []
    for image in flash_images:
        if isinstance(image, str):
            resolved_images.append(env.subst(image))
        else:
            resolved_images.append(str(image))

    cmd = [
        env.subst("$PYTHONEXE"),
        env.subst("$OBJCOPY"),
        "--chip",
        BOARD_CONFIG.get("build.mcu", "esp32"),
        "merge_bin",
        "--fill-flash-size",
        BOARD_CONFIG.get("upload.flash_size", "4MB"),
        "-o",
        env.subst(MERGED_BIN),
    ] + resolved_images

    env.Execute(_format_command(cmd))


if not IS_UPLOADFS_TARGET:
    env.AddPreAction(APP_BIN, build_littlefs)
    env.AddPostAction(APP_BIN, merge_bin)
    env.Replace(
        UPLOADERFLAGS=["write_flash", "0x0", MERGED_BIN],
        UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS',
    )

