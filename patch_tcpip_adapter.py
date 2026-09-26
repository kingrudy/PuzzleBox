#!/usr/bin/env python3
"""
Post-install script to patch ESP32 Arduino Core 2.x tcpip_adapter.h bug.

The SDK header uses ip6_addr_t without including lwip/ip6_addr.h.
This script adds the missing include automatically after platform installation.
"""

Import("env")
import os
import re

def patch_tcpip_adapter(*args, **kwargs):
    """Add missing ip6_addr_t include to tcpip_adapter.h"""
    
    print("=" * 60)
    print("Running tcpip_adapter.h patch script...")
    print("=" * 60)
    
    platform_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    if not platform_dir:
        print("⚠ Warning: Could not find framework-arduinoespressif32 package")
        return
    
    print(f"Platform dir: {platform_dir}")
    
    # Patch both ESP32 and ESP32-S3 SDK headers
    chip_variants = ["esp32", "esp32s3", "esp32c3"]
    
    for chip in chip_variants:
        header_path = os.path.join(
            platform_dir,
            "tools", "sdk", chip, "include", "tcpip_adapter", "include", "tcpip_adapter.h"
        )
        
        if not os.path.exists(header_path):
            print(f"  Skipping {chip}: header not found")
            continue
        
        print(f"  Patching {chip}...")
        
        # Read the header file
        with open(header_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Check if already patched
        if 'BOXBOX_PATCHED' in content:
            print(f"    ✓ {chip} already patched")
            continue
        
        # Strategy: Comment out the two problematic function declarations
        # These are deprecated IPv6 functions that we don't need
        content = re.sub(
            r'esp_err_t tcpip_adapter_get_ip6_linklocal\([^)]+\);',
            r'// BOXBOX_PATCHED: Commented out deprecated IPv6 function\n// esp_err_t tcpip_adapter_get_ip6_linklocal(tcpip_adapter_if_t tcpip_if, ip6_addr_t *if_ip6);',
            content
        )
        
        content = re.sub(
            r'esp_err_t tcpip_adapter_get_ip6_global\([^)]+\);',
            r'// BOXBOX_PATCHED: Commented out deprecated IPv6 function\n// esp_err_t tcpip_adapter_get_ip6_global(tcpip_adapter_if_t tcpip_if, ip6_addr_t *if_ip6);',
            content
        )
        
        # Write the patched content
        with open(header_path, 'w', encoding='utf-8') as f:
            f.write(content)
        
        print(f"    ✓ Successfully patched {chip}")
    
    print("=" * 60)

# Run the patch before any compilation starts
patch_tcpip_adapter()
