"""Build original modular Magma Rift: terrain, effects and collisions share one grid."""
from pathlib import Path
import xml.etree.ElementTree as E
import struct
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/"assets/Maps/MagmaRift"
W,H,T=75,50,64
solid=set()
for y in range(5,45):
 for x in range(5,70):
  if min(x+y,74-x+y,x+49-y,74-x+49-y)<14:continue
  pools=[(31,14),(43,35)]
  if any(abs(x-cx)<=5 and abs(y-cy)<=4 and abs(x-cx)+abs(y-cy)<=7 for cx,cy in pools):continue
  solid.add((x,y))
root=E.Element("map",version="1.10",tiledversion="1.12.2",orientation="orthogonal",renderorder="right-down",width=str(W),height=str(H),tilewidth="64",tileheight="64",infinite="0",backgroundcolor="#411c21")
fw,fh=struct.unpack(">II",(DEST/"basalt-floor.png").read_bytes()[16:24])
cols,rows=fw//64,fh//64
count=cols*rows
E.SubElement(root,"tileset",firstgid="1",source="floor.tsx")
lw,lh=struct.unpack(">II",(DEST/"lava-flow.png").read_bytes()[16:24])
lcols,lrows=lw//64,lh//64
lavaCount=lcols*lrows
E.SubElement(root,"tileset",firstgid=str(count+1),source="lava.tsx")
E.SubElement(root,"tileset",firstgid=str(count+lavaCount+1),source="cover.tsx")
E.SubElement(root,"tileset",firstgid=str(count+lavaCount+2),source="shore.tsx")
layerid=0
objectid=0

def layer(name,tiles,kind):
 global layerid
 layerid+=1
 node=E.SubElement(root,"layer",id=str(layerid),name=name,width=str(W),height=str(H),attrib={"class":kind})
 E.SubElement(node,"data",encoding="csv").text="\n"+",\n".join(
  ",".join(str(tiles[y*W+x]) for x in range(W)) for y in range(H))+"\n"

def group(name,kind="",visible="1"):
 global layerid
 layerid+=1
 return E.SubElement(root,"objectgroup",id=str(layerid),name=name,attrib={"class":kind},visible=visible,opacity="0")

def rect(parent,name,x,y,w,h):
 global objectid
 objectid+=1
 E.SubElement(parent,"object",id=str(objectid),name=name,x=str(x),y=str(y),width=str(w),height=str(h))

layer("Magma bed",[count+1+(y%lrows)*lcols+x%lcols for y in range(H) for x in range(W)],"ground_blend")
floor=[]
for y in range(H):
 for x in range(W):
  if (x,y) not in solid:floor.append(0);continue
  mask=sum(bit for dx,dy,bit in [(0,-1,1),(1,0,2),(0,1,4),(-1,0,8)] if (x+dx,y+dy) not in solid)
  floor.append(1+(y%rows)*cols+x%cols)
layer("Basalt platforms",floor,"ground_blend")
rocks=[(21,20),(52,20),(21,28),(52,28),(36,22),(37,27),(18,10),(55,10),(18,38),(55,38)]
cover=group("Basalt cover","decor");cover.set("opacity","1")
for x,y in rocks:
 objectid+=1
 E.SubElement(cover,"object",id=str(objectid),name="Basalt cover",gid=str(count+lavaCount+1),x=str(x*T-36),y=str(y*T+140),width="200",height="180")
shore=group("Basalt ledges","shore")
for x,y in sorted(solid):
 for dx,dy,side in [(0,-1,"N"),(1,0,"E"),(0,1,"S"),(-1,0,"W")]:
  if (x+dx,y+dy) in solid:continue
  if side=="N":rect(shore,side,x*T,y*T,64,10)
  elif side=="S":rect(shore,side,x*T,y*T+54,64,10)
  elif side=="W":rect(shore,side,x*T,y*T,10,64)
  else:rect(shore,side,x*T+54,y*T,10,64)
collision=group("collision",visible="0")
flow=group("Lava surface","lava")
# Merge adjacent lava cells across each row. These rectangles are also the visual shader mask.
for y in range(H):
 x=0
 while x<W:
  if (x,y) in solid:x+=1;continue
  start=x
  while x<W and (x,y) not in solid:x+=1
  rect(collision,"Lava boundary",start*T,y*T,(x-start)*T,T)
  rect(flow,"Magma flow",start*T,y*T,(x-start)*T,T)
# The stone bank occupies a short ledge on the land side of each lava boundary.
for x,y in sorted(solid):
 for dx,dy,side in [(0,-1,"N"),(1,0,"E"),(0,1,"S"),(-1,0,"W")]:
  if (x+dx,y+dy) in solid:continue
  if side=="N":rect(collision,"Stone bank",x*T,y*T,64,18)
  elif side=="S":rect(collision,"Stone bank",x*T,y*T+46,64,18)
  elif side=="W":rect(collision,"Stone bank",x*T,y*T,18,64)
  else:rect(collision,"Stone bank",x*T+46,y*T,18,64)
for x,y in rocks:rect(collision,"Rock foot",x*T+12,y*T+88,104,24)
spawns=group("spawns",visible="0")
for i,(x,y) in enumerate([(12,10),(40,8),(62,10),(12,23),(62,23),(12,37),(35,41),(62,37),
                         (23,18),(50,18),(24,32),(51,32),(40,14),(32,34),(24,40),(50,40)]):
 assert (x,y) in solid
 objectid+=1
 E.SubElement(E.SubElement(spawns,"object",id=str(objectid),name="Spawn %02d"%i,x=str(x*T+32),y=str(y*T-68)),"point")
embers=group("Magma embers","embers")
for x,y in [(29,12),(34,14),(29,16),(40,33),(45,35),(43,38),(3,20),(72,29),(18,47),(60,2)]:
 objectid+=1
 E.SubElement(E.SubElement(embers,"object",id=str(objectid),x=str(x*T+32),y=str(y*T+32)),"point")
vents=group("Smoke vents","vents")
for x,y in [(26,13),(36,16),(38,34),(48,37),(5,7),(68,42)]:
 objectid+=1
 E.SubElement(E.SubElement(vents,"object",id=str(objectid),x=str(x*T),y=str(y*T)),"point")
root.set("nextlayerid",str(layerid+1));root.set("nextobjectid",str(objectid+1))
lavaSet=E.Element("tileset",version="1.10",tiledversion="1.12.2",name="Molten lava",tilewidth="64",tileheight="64",tilecount=str(lavaCount),columns=str(lcols))
E.SubElement(lavaSet,"image",source="lava-flow.png",width=str(lw),height=str(lh))
floorSet=E.Element("tileset",version="1.10",tiledversion="1.12.2",name="Basalt terrain",tilewidth="64",tileheight="64",tilecount=str(count),columns=str(cols))
E.SubElement(floorSet,"image",source="basalt-floor.png",width=str(fw),height=str(fh))
cw,ch=struct.unpack(">II",(DEST/"basalt-cover.png").read_bytes()[16:24])
coverSet=E.Element("tileset",version="1.10",tiledversion="1.12.2",name="Basalt cover",tilewidth=str(cw),tileheight=str(ch),tilecount="1",columns="0")
E.SubElement(E.SubElement(coverSet,"tile",id="0"),"image",source="basalt-cover.png",width=str(cw),height=str(ch))
sw,sh=struct.unpack(">II",(DEST/"basalt-rim.png").read_bytes()[16:24])
shoreSet=E.Element("tileset",version="1.10",tiledversion="1.12.2",name="Basalt rim",tilewidth=str(sw),tileheight=str(sh),tilecount="1",columns="0")
E.SubElement(E.SubElement(shoreSet,"tile",id="0"),"image",source="basalt-rim.png",width=str(sw),height=str(sh))
for node,path in [(shoreSet,DEST/"shore.tsx"),(root,DEST/"map.tmx"),(lavaSet,DEST/"lava.tsx"),(floorSet,DEST/"floor.tsx"),(coverSet,DEST/"cover.tsx")]:
 E.indent(node,space=" ");E.ElementTree(node).write(path,encoding="UTF-8",xml_declaration=True)
print("Built modular MagmaRift from",len(solid),"walkable cells")
