"""Native-pixel diagnostic crops for inspecting the playing sequence (not proofs)."""
import argparse
from pathlib import Path
from PIL import Image,ImageDraw

p=argparse.ArgumentParser();p.add_argument('frames',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
selected=list(range(60,419,12))
for start in range(0,len(selected),6):
    sheet=Image.new('RGB',(720,1140),'#202020');draw=ImageDraw.Draw(sheet)
    for j,index in enumerate(selected[start:start+6]):
        f=a.frames/f'frame_{index:05d}.bmp'
        with Image.open(f) as image:crop=image.crop((180,60,540,420))
        x=j%2*360;y=j//2*380;sheet.paste(crop,(x,y+20));draw.text((x+5,y+3),f'{a.frames.name} | frame {index} | song {30+index/60:.2f}s',fill='white')
    sheet.save(a.output/f'{start//6:02d}.png')
