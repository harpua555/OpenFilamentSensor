#!/usr/bin/env python3
"""
Shared board configuration module for Centauri Carbon Motion Detector.
Single source of truth for board-to-chip-family mappings and related functions.
"""

from __future__ import annotations
from typing import Dict, Optional

# Board to chip family mapping for CHIP_FAMILY environment variable
# Format: <optionalmanufacturer> <chipbase>-<chiptype>
# Examples: "ESP32", "ESP32-S3", "Seeed ESP32-C3"
BOARD_TO_CHIP_FAMILY: Dict[str, str] = {
    "esp32s3": "ESP32-S3",
    "esp32c3": "ESP32-C3",
    "esp32": "ESP32 (WROOM)",
    "seeed_esp32s3": "Seeed ESP32-S3",
    "seeed_esp32c3": "Seeed ESP32-C3",
    "esp32c3supermini": "ESP32-C3",
}

def get_chip_family_for_board(board_env: str) -> str:
    """
    Get the chip family for a given PlatformIO environment name.
    Returns the appropriate chip family string or raises ValueError if unknown.

    Args:
        board_env: PlatformIO environment name (e.g., "esp32s3")

    Returns:
        Chip family string (e.g., "ESP32-S3", "seeed ESP32-C3")

    Raises:
        ValueError: If board_env is not supported
    """
    chip_family = BOARD_TO_CHIP_FAMILY.get(board_env)
    if chip_family is None:
        raise ValueError(f"Unknown PlatformIO environment '{board_env}'. "
                        f"Supported environments: {sorted(BOARD_TO_CHIP_FAMILY.keys())}")
    return chip_family

def validate_board_environment(board_env: str) -> bool:
    """
    Validate that board environment is known and has a valid chip family.

    Args:
        board_env: PlatformIO environment name

    Returns:
        True if board is supported, False otherwise
    """
    return board_env in BOARD_TO_CHIP_FAMILY

def get_supported_boards() -> list[str]:
    """
    Get list of all supported board environments.

    Returns:
        Sorted list of supported board environment names
    """
    return sorted(BOARD_TO_CHIP_FAMILY.keys())

def get_supported_chip_families() -> set[str]:
    """
    Return the set of unique chip family identifiers supported by the board mapping.
    
    Returns:
        set[str]: Unique chip family strings present in the board-to-chip-family mapping.
    """
    return set(BOARD_TO_CHIP_FAMILY.values())


def compose_chip_family_label(board_env: str, chip_prefix: Optional[str]) -> str:
    """
    Produce a chip family label by combining a user-provided prefix with the board's default suffix.
    
    Parameters:
        board_env (str): PlatformIO board environment name used to look up the board's default chip family label.
        chip_prefix (Optional[str]): User-supplied prefix to use for the chip family label; may be None or whitespace.
    
    Returns:
        final_label (str): The resulting chip family label. If `chip_prefix` is empty or whitespace, returns the board's default label.
        If `chip_prefix` already ends with the default label's suffix (the substring from the first '-' onward), returns `chip_prefix`.
        Otherwise returns `chip_prefix` concatenated with the default suffix.
    """
    base_label = get_chip_family_for_board(board_env)
    if not chip_prefix:
        return base_label

    prefix = chip_prefix.strip()
    if not prefix:
        return base_label

    suffix = ""
    hyphen_index = base_label.find("-")
    if hyphen_index != -1:
        suffix = base_label[hyphen_index:]

    if suffix and prefix.endswith(suffix):
        return prefix

    return f"{prefix}{suffix}"
