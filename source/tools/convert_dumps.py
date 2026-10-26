import glob
import os
import re
from PIL import Image

def main():
    dump_dir = "out/dump"
    if not os.path.exists(dump_dir):
        print(f"Directory {dump_dir} does not exist.")
        return

    files = glob.glob(os.path.join(dump_dir, "*.raw"))
    if not files:
        print(f"No .raw files found in {dump_dir}.")
        return

    for raw_path in files:
        png_path = raw_path[:-4] + ".png"
        m = re.search(r'(\d+)x(\d+)\.raw$', raw_path)
        if not m:
            continue
        w, h = int(m.group(1)), int(m.group(2))
        try:
            with open(raw_path, 'rb') as f:
                data = f.read()
            if len(data) >= w * h * 4:
                # Vulkan A8R8G8B8Srgb is stored as BGRA in memory
                img = Image.frombytes('RGBA', (w, h), data[:w*h*4], 'raw', 'BGRA')
                img.save(png_path)
                print(f"Converted {raw_path} -> {png_path} ({w}x{h})")
        except Exception as e:
            print(f"Error converting {raw_path}: {e}")

if __name__ == '__main__':
    main()
