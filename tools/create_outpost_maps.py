"""Author two modular TMX arenas; props, footprints and spawns share coordinates.

Generated artwork is kept intact. PNG alpha is read only to fit each atlas cell.
Run from any directory with Python's standard library.
"""
from pathlib import Path
import struct
import zlib
import xml.etree.ElementTree as E
from collections import deque

ROOT = Path(__file__).resolve().parents[1]
W, H, T = 75, 50, 64


def png_info(path, alpha=False):
    raw = path.read_bytes()
    width, height, depth, color = struct.unpack('>IIBB', raw[16:26])
    if not alpha:
        return width, height
    assert depth == 8 and color == 6, (path, depth, color)
    data, offset = b'', 8
    while offset < len(raw):
        size = int.from_bytes(raw[offset:offset+4], 'big')
        if raw[offset+4:offset+8] == b'IDAT':
            data += raw[offset+8:offset+8+size]
        offset += size + 12
    decoded = zlib.decompress(data)
    stride, previous, cursor, cells = width*4, bytearray(width*4), 0, [None]*4
    def paeth(a, b, c):
        p = a+b-c
        distances = [abs(p-a), abs(p-b), abs(p-c)]
        return (a, b, c)[distances.index(min(distances))]
    for y in range(height):
        mode = decoded[cursor]
        row = bytearray(decoded[cursor+1:cursor+1+stride])
        cursor += stride+1
        for i in range(stride):
            a, b, c = (row[i-4] if i >= 4 else 0), previous[i], (previous[i-4] if i >= 4 else 0)
            value = (0, a, b, (a+b)//2, paeth(a,b,c))[mode]
            row[i] = (row[i]+value) & 255
        for x in range(width):
            if row[x*4+3] < 64:
                continue
            cell = (y//(height//2))*2+x//(width//2)
            cx, cy = x % (width//2), y % (height//2)
            box = cells[cell]
            cells[cell] = [cx,cy,cx,cy] if box is None else [min(box[0],cx),min(box[1],cy),max(box[2],cx),max(box[3],cy)]
        previous = row
    return width, height, cells


def build(name, industrial):
    dest = ROOT/'assets/Maps'/name
    fw, fh = png_info(dest/'floor.png')
    aw, ah, boxes = png_info(dest/'props.png', True)
    cols, rows = fw//64, fh//64
    count = cols*rows
    root = E.Element('map', version='1.10', tiledversion='1.12.2', orientation='orthogonal', renderorder='right-down', width=str(W), height=str(H), tilewidth='64', tileheight='64', infinite='0')
    E.SubElement(root,'tileset',firstgid='1',source='floor.tsx')
    E.SubElement(root,'tileset',firstgid=str(count+1),source='props.tsx')
    E.SubElement(root,'tileset',firstgid=str(count+5),source='wall.tsx')
    floor = E.SubElement(root,'layer',id='1',name='Walkable floor',width=str(W),height=str(H),attrib={'class':'ground_blend'})
    E.SubElement(floor,'data',encoding='csv').text='\n'+',\n'.join(','.join(str(1+(y%rows)*cols+x%cols) for x in range(W)) for y in range(H))+'\n'
    layer_id, object_id = 1, 0
    def group(label, kind, visible=True):
        nonlocal layer_id
        layer_id += 1
        return E.SubElement(root,'objectgroup',id=str(layer_id),name=label,attrib={'class':kind},visible='1' if visible else '0',opacity='1' if kind=='decor' else '0')
    def obj(parent,label,x,y,width=0,height=0,gid=None,point=False):
        nonlocal object_id
        object_id += 1
        attrs=dict(id=str(object_id),name=label,x=str(round(x,3)),y=str(round(y,3)))
        if point:
            E.SubElement(E.SubElement(parent,'object',**attrs),'point')
        else:
            attrs.update(width=str(round(width,3)),height=str(round(height,3)))
            if gid is not None: attrs['gid']=str(gid)
            return E.SubElement(parent,'object',**attrs)
    architecture = group('Steel structure' if industrial else 'Sandstone ruins','architecture_foundry' if industrial else 'architecture_desert')
    markings = group('Service walkways' if industrial else 'Dust trails','markings_foundry' if industrial else 'markings_desert')
    collision = group('collision','',False)
    obstacles=[]
    def wall(x,y,w,h):
        obstacles.append((x,y,w,h))
        obj(architecture,'Wall',x,y,w,h)
        obj(collision,'Wall footprint',x,y,w,h)
    wall(0,0,4800,256); wall(0,2944,4800,256)
    wall(0,256,256,2688); wall(4544,256,256,2688)
    if industrial:
        walls=[(1408,768,640,80),(2752,768,640,80),(1408,2304,640,80),(2752,2304,640,80),
               (1280,960,80,320),(3440,960,80,320),(1280,1856,80,320),(3440,1856,80,320),
               (2240,576,320,80),(2240,2544,320,80)]
        props=[(0,1000,900,260),(0,3800,900,260),(0,1000,2240,260),(0,3800,2240,260),
               (1,600,600,310),(1,4200,600,310),(1,600,2730,310),(1,4200,2730,310),
               (2,1870,1180,300),(2,2930,2100,300),(2,1870,2100,300),(2,2930,1180,300),
               (3,2400,1040,300),(3,2400,2240,300),(0,720,1850,220),(0,4080,1850,220)]
        footprints=[.25,.30,.42,.32]
        for r in [(448,1344,3904,320),(2272,400,256,2400)]:obj(markings,'Service lane',*r)
    else:
        walls=[(1408,768,512,80),(1408,768,80,448),(2880,768,512,80),(3312,768,80,448),
               (1408,2352,512,80),(1408,1984,80,448),(2880,2352,512,80),(3312,1984,80,448)]
        props=[(0,650,650,330),(0,4150,650,330),(0,650,2700,330),(0,4150,2700,330),
               (1,2160,720,300),(1,2640,2500,300),(2,1100,2180,340),(2,3700,1000,340),
               (3,1850,1160,290),(3,2950,2050,290),(3,1850,2050,290),(3,2950,1160,290),
               (0,2350,1100,260),(0,2450,2260,260),(1,850,1860,260),(1,3950,1860,260)]
        footprints=[.27,.42,.76,.40]
        # Desert routes stay unpainted; wind particles provide movement without a cross overlay.
    for r in walls:wall(*r)
    decor=group('Authored cover','decor')
    for kind,cx,bottom,size in props:
        left,top,right,low=boxes[kind]
        scale=size/(aw//2)
        x=cx-(left+right)*.5*scale
        y=bottom+size-(low+1)*scale
        sprite=obj(decor,'Cover '+str(kind),x,y,size,size,count+1+kind)
        E.SubElement(E.SubElement(sprite,'properties'),'property',name='ground_y',type='float',value=str(bottom))
        pw=(right-left+1)*scale*.94
        ph=(low-top+1)*scale*footprints[kind]
        rect=(cx-pw/2,bottom-ph,pw,ph)
        obstacles.append(rect);obj(collision,'Cover foot',*rect)
    spawns=group('spawns','',False)
    candidates=[(768,800),(2400,480),(4032,800),(768,1472),(4032,1472),(768,2560),(2400,2720),(4032,2560),
                (1600,1500),(3200,1500),(1600,1920),(3200,1920),(2048,1900),(2752,1900),(2048,1310),(2752,1310)]
    def clear(x,y,pad=30):
        return all(not (a-pad<x<a+w+pad and b-pad<y<b+h+pad) for a,b,w,h in obstacles)
    for i,(x,y) in enumerate(candidates):
        assert clear(x,y), (name,'blocked spawn',i,x,y)
        obj(spawns,'Spawn %02d'%i,x,y-68,point=True)
    # Verify the two flag bases and all outposts connect through generous walkable lanes.
    walk={(x,y) for y in range(4,46) for x in range(4,71) if clear(x*T+32,y*T+32,24)}
    start=min(walk,key=lambda p:abs(p[0]*T-768)+abs(p[1]*T-1472))
    visited={start};queue=deque([start])
    while queue:
        x,y=queue.popleft()
        for p in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
            if p in walk and p not in visited:visited.add(p);queue.append(p)
    assert len(visited)==len(walk),(name,'isolated walkable pocket')
    emit=group('Steam and furnace glow' if industrial else 'Windblown dust','foundry_fx' if industrial else 'desert_fx')
    for x,y in ([(600,590),(4200,590),(600,2720),(4200,2720),(2400,980),(2400,2180)] if industrial else [(520,900),(1450,500),(2400,1450),(3300,2700),(4250,2200)]):
        obj(emit,'Emitter',x,y,point=True)
    root.set('nextlayerid',str(layer_id+1));root.set('nextobjectid',str(object_id+1))
    floor_set=E.Element('tileset',version='1.10',name='Steel plates' if industrial else 'Sandstone paving',tilewidth='64',tileheight='64',tilecount=str(count),columns=str(cols))
    E.SubElement(floor_set,'image',source='floor.png',width=str(fw),height=str(fh))
    prop_set=E.Element('tileset',version='1.10',name='Foundry props' if industrial else 'Desert props',tilewidth=str(aw//2),tileheight=str(ah//2),tilecount='4',columns='2')
    E.SubElement(prop_set,'image',source='props.png',width=str(aw),height=str(ah))
    ww,wh=png_info(dest/'wall.png')
    wall_set=E.Element('tileset',version='1.10',name='Arena wall',tilewidth=str(ww),tileheight=str(wh),tilecount='1',columns='0')
    E.SubElement(E.SubElement(wall_set,'tile',id='0'),'image',source='wall.png',width=str(ww),height=str(wh))
    for node,file in [(root,'map.tmx'),(floor_set,'floor.tsx'),(prop_set,'props.tsx'),(wall_set,'wall.tsx')]:
        E.indent(node,space=' ');E.ElementTree(node).write(dest/file,encoding='UTF-8',xml_declaration=True)
    (dest/'README.md').write_text(f'''# {name}

Original modular 4800 x 3200 arena, matching MagmaRift's faceted game-art direction.
Floor and four isolated props were generated with imagegen using the MagmaRift floor as a style reference.
Layout, modular wall strips, floor markings and effects are authored separately. Nothing is painted over a single map image.

Edit map.tmx in Tiled; rerun tools/create_outpost_maps.py to regenerate the authored layout.
The collision layer shares exact wall rectangles and alpha-fitted prop feet with the visuals. Each cover sprite carries ground_y so transparent atlas padding never affects depth ordering.
Sixteen checked spawn points and a continuous center delivery lane support FFA, TDM, elimination and flag mode.
Effect emitters use class {'foundry_fx' if industrial else 'desert_fx'} and respect the Effects setting.
''',encoding='utf-8')
    print(name,len(obstacles),'obstacles,',len(visited),'connected navigation cells')


if __name__=='__main__':
    build('AbandonedFoundry',True)
    build('DesertOutpost',False)
