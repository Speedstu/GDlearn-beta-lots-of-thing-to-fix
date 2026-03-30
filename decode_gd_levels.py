#!/usr/bin/env python3
"""
GD Level Decoder - Converts Geometry Dash official levels to plain text
Usage: python decode_gd_levels.py <input_file> <output_file>
"""

import sys
import base64
import gzip
import os

def decode_gd_level(input_path, output_path):
    """Decode a GD level file from base64+gzip to plain text"""
    try:
        with open(input_path, 'r', encoding='utf-8') as f:
            encoded = f.read().strip()
        
        # Decode base64
        compressed = base64.urlsafe_b64decode(encoded + '==')
        
        # Decompress gzip
        decompressed = gzip.decompress(compressed)
        
        # Write output
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(decompressed.decode('utf-8', errors='ignore'))
        
        print(f"[OK] Decoded {input_path} -> {output_path}")
        print(f"     Size: {len(decompressed)} bytes")
        return True
        
    except Exception as e:
        print(f"[ERROR] Failed to decode {input_path}: {e}")
        return False

def decode_all_levels():
    """Decode all official GD levels"""
    gd_path = r"G:\game\Geometry Dash (Build 21578706)\Resources\levels"
    output_dir = r"G:\gd-ml-bot\levels"
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Process all level files
    success_count = 0
    for filename in os.listdir(gd_path):
        if filename.endswith('.txt'):
            input_file = os.path.join(gd_path, filename)
            output_file = os.path.join(output_dir, filename.replace('.txt', '.gmd'))
            
            if decode_gd_level(input_file, output_file):
                success_count += 1
    
    print(f"\n[Summary] Successfully decoded {success_count} levels to {output_dir}")

if __name__ == "__main__":
    if len(sys.argv) == 1:
        # No arguments - decode all official levels
        decode_all_levels()
    elif len(sys.argv) == 3:
        # Single file mode
        decode_gd_level(sys.argv[1], sys.argv[2])
    else:
        print("Usage: python decode_gd_levels.py [input_file output_file]")
        print("       (no args = decode all levels)")
