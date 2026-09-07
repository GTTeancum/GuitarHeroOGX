"""Encode native screenshot frames without altering their visual contents."""
import argparse
from pathlib import Path
import subprocess

p=argparse.ArgumentParser();p.add_argument('frames',type=Path);p.add_argument('output',type=Path);p.add_argument('--fps',type=int,default=30);a=p.parse_args()
frames=sorted(a.frames.glob('frame_*.bmp'))
assert len(frames)>=2
listing=a.frames/'frames.ffconcat'
listing.write_text('ffconcat version 1.0\n'+''.join("file '"+f.as_posix().replace("'","'\\''")+f"'\nduration {1/a.fps:.9f}\n" for f in frames)+"file '"+frames[-1].as_posix()+"'\n")
result=subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-safe','0','-f','concat','-i',str(listing),'-an','-c:v','libx264','-preset','medium','-crf','17','-pix_fmt','yuv420p','-r',str(a.fps),'-t',str(len(frames)/a.fps),'-movflags','+faststart',str(a.output)],capture_output=True,text=True)
if result.returncode:raise RuntimeError(result.stderr[-1500:])
print(f'{len(frames)} native frames, {len(frames)/a.fps:.2f}s, {a.output.stat().st_size} bytes: {a.output.name}')
