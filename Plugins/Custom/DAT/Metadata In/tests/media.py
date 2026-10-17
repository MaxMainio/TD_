"""Synthetic media coverage tests. Python standard library + bundled readers only."""
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import wave
import zlib


def exr(path, multipart=False):
    def attr(name, kind, data):
        return name.encode()+b"\0"+kind.encode()+b"\0"+struct.pack("<I",len(data))+data
    def header(name):
        channels = b"R\0" + struct.pack("<iB3xii",2,0,1,1) + b"\0"
        out = attr("channels","chlist",channels)+attr("compression","compression",b"\0")
        for key in ("dataWindow","displayWindow"):
            out += attr(key,"box2i",struct.pack("<iiii",0,0,0,0))
        out += attr("lineOrder","lineOrder",b"\0")+attr("pixelAspectRatio","float",struct.pack("<f",1))
        out += attr("screenWindowCenter","v2f",struct.pack("<ff",0,0))+attr("screenWindowWidth","float",struct.pack("<f",1))
        out += attr("customMetadata","string",("part-"+name).encode())
        if multipart:
            out += attr("name","string",name.encode())+attr("type","string",b"scanlineimage")+attr("chunkCount","int",struct.pack("<i",1))
        return out+b"\0"
    heads = header("first") + (header("second")+b"\0" if multipart else b"")
    magic = struct.pack("<II",20000630,2 | (0x1000 if multipart else 0))
    offset = len(magic)+len(heads)+(16 if multipart else 8)
    if multipart:
        data = magic+heads+struct.pack("<QQ",offset,offset+16)
        data += b"".join(struct.pack("<iii f",i,0,4,0.5) for i in (0,1))
    else:
        data = magic+heads+struct.pack("<Q",offset)+struct.pack("<ii f",0,4,0.5)
    path.write_bytes(data)


def make_fixtures(root):
    root.mkdir(parents=True, exist_ok=True)
    encoded = json.loads(Path(__file__).with_name("media-fixtures.json").read_text())
    for suffix, content in encoded.items():
        (root / ("synthetic."+suffix)).write_bytes(zlib.decompress(base64.b64decode(content)))
    with wave.open(str(root/"synthetic.wav"),"wb") as f:
        f.setparams((1,2,8000,0,"NONE","not compressed")); f.writeframes(b"\0"*1600)
    exr(root/"synthetic.exr")
    exr(root/"multipart.exr",True)
    cards = ["SIMPLE  =                    T", "BITPIX  =                   16", "NAXIS   =                    2",
             "NAXIS1  =                    1", "NAXIS2  =                    1", "OBJECT  = 'MetaData_In fixture'", "END"]
    fits="".join(c.ljust(80) for c in cards).ljust(2880)
    (root/"synthetic.fits").write_bytes(fits.encode()+b"\0"*2880)


def main():
    probe, readers, folder = sys.argv[1:]
    readers, root = Path(readers),Path(folder)
    make_fixtures(root)
    exiftool=["/usr/bin/perl",str(readers/"exiftool/exiftool"),"-config",""]
    for suffix in ("jpg","png","tiff"):
        subprocess.run(exiftool+["-overwrite_original", "-EXIF:Make=FixtureCamera", "-XMP-tiff:Make=XMPCamera",
            "-EXIF:Artist=Fixture Author", "-XMP-dc:Title=Metadata caf\u00e9", "-XMP-dc:Subject=alpha", "-XMP-dc:Subject+=beta",
            "-GPSLatitude=12.5","-GPSLatitudeRef=N",str(root/("synthetic."+suffix))],check=True,capture_output=True)
    for path in sorted(root.glob("*")):
        if not path.is_file(): continue
        run=subprocess.run([probe,str(readers),str(path)],check=True,capture_output=True,text=True,
                           env=dict(os.environ,PATH="/usr/bin:/bin"))
        result=json.loads(run.stdout)
        rows=result["rows"]
        assert result["status"]=="ready", (path.name,result["warnings"])
        keys=[r["key"] for r in rows]
        assert len(keys)==len(set(keys)), path.name
        assert any(r["source"]=="exiftool" for r in rows), path.name
        if path.suffix in (".mov",".mp4",".wav",".mp3",".flac"):
            assert any(r["key"].endswith("codec_name") for r in rows),path.name
            assert any(r["key"].endswith("sample_rate") for r in rows),path.name
        if path.suffix in (".mov",".mp4"):
            assert any(r["key"]=="media.streams[1].codec_name" for r in rows),path.name
        if path.suffix in (".jpg",".png",".tiff"):
            assert any(r["value"]=="FixtureCamera" for r in rows), path.name
            assert any(r["value"]=="XMPCamera" for r in rows), path.name
            assert any(r["value"]=="Metadata caf\u00e9" for r in rows),path.name
            assert any(r["value"]=="beta" for r in rows),path.name
        if path.name=="multipart.exr":
            assert {"part-first","part-second"} <= {r["value"] for r in rows}
        print("PASS",path.name,len(rows),"rows")


if __name__=="__main__": main()
