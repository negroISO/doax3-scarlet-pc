"""Draws the port's icon (\"1886\" in brass on a dark slate field) as .ico and .png. Needs Pillow.
No art from the game is used. usage: make_theorder_icon.py launcher/theorder.ico launcher/theorder.png"""
# Historical upstream artwork generator; the DOAX3 package uses launcher/doax3.*.
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

S = 1024  # drawn large, scaled down for every icon size
BRASS, BRASS_HI = (168, 128, 60, 255), (214, 176, 98, 255)


def serif(size):
    for name in ('georgiab.ttf', 'georgia.ttf', 'DejaVuSerif-Bold.ttf', 'LiberationSerif-Bold.ttf'):
        for folder in (Path('C:/Windows/Fonts'), Path('/usr/share/fonts/truetype/dejavu'),
                       Path('/usr/share/fonts/truetype/liberation')):
            if (folder / name).is_file():
                return ImageFont.truetype(str(folder / name), size)
    return ImageFont.load_default()


def draw():
    image = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((24, 24, S - 24, S - 24), radius=190, fill=255)
    field = Image.new('RGBA', (S, S))
    fd = ImageDraw.Draw(field)
    for y in range(S):  # slate, darker towards the bottom
        t = y / S
        fd.line((0, y, S, y), fill=(int(40 - 26 * t), int(44 - 28 * t), int(52 - 32 * t), 255))
    image.paste(field, (0, 0), mask)
    d = ImageDraw.Draw(image)
    d.rounded_rectangle((70, 70, S - 70, S - 70), radius=150, outline=BRASS, width=26)
    font = serif(330)
    text = '1886'
    box = d.textbbox((0, 0), text, font=font)
    x = (S - (box[2] - box[0])) / 2 - box[0]
    y = (S - (box[3] - box[1])) / 2 - box[1]
    d.text((x + 6, y + 8), text, font=font, fill=(0, 0, 0, 160))
    d.text((x, y), text, font=font, fill=BRASS_HI)
    return image


def main():
    ico, png = sys.argv[1], sys.argv[2]
    image = draw()
    image.resize((256, 256), Image.LANCZOS).save(png)
    image.save(ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])


if __name__ == '__main__':
    main()
