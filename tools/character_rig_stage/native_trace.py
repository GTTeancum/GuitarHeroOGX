"""Read raw native traces and legacy PowerShell UTF-16 wrapped traces."""
from pathlib import Path

def lines(path):
    data=Path(path).read_bytes()
    wrapped=data.startswith((b'\xff\xfe',b'\xfe\xff'))
    text=data.decode('utf-16' if wrapped else 'utf-8',errors='replace')
    if not wrapped:return text.splitlines()
    output=[]
    for row in text.splitlines():
        if row.startswith('[') or not output:output.append(row)
        else:output[-1]=output[-1].rstrip()+' '+row.strip()
    return output
