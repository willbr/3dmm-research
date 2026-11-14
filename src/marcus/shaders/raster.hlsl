// Marcus GPU rasterization. BRender supplies transformed, lit and clipped triangles.
struct Triangle {
    float4 position[3];
    float4 varying[3];
    uint4 texture;
    uint4 shade;
    int4 scanY, scanEdges, scanBottom;
    uint4 scanParameter[4];
};
#if defined(COMPUTE)
StructuredBuffer<Triangle> triangles : register(t0, space0);
StructuredBuffer<uint> texels : register(t1, space0);
StructuredBuffer<uint> bins : register(t2, space0);
RWStructuredBuffer<uint> output : register(u0, space1);
cbuffer Parameters : register(b0, space2)
#elif defined(VERTEX)
StructuredBuffer<Triangle> triangles : register(t0, space0);
cbuffer Parameters : register(b0, space1)
#else
StructuredBuffer<Triangle> triangles : register(t0, space2);
StructuredBuffer<uint> texels : register(t1, space2);
StructuredBuffer<uint> seed : register(t2, space2);
cbuffer Parameters : register(b0, space3)
#endif
{
    uint width, height, tilesX, background;
    uint mode, overlay, filterMode, paletteOffset;
    float nearClip, farClip, tangentX, tangentY;
    uint orthographic, opaqueBackground;
    float orthoHeight, wireWidth;
    uint firstTriangle, padding0, padding1, padding2;
};

#if !defined(VERTEX)
float4 unpack(uint value) {
    return float4(value & 255, (value >> 8) & 255, (value >> 16) & 255, value >> 24) / 255.0;
}
uint pack(float4 value) {
    uint4 c = uint4(saturate(value) * 255.0 + 0.5);
    return c.x | (c.y << 8) | (c.z << 16) | (c.w << 24);
}
float4 paletteColour(uint index) { return unpack(texels[paletteOffset + index]); }
int wrap(int value, uint size) {
    uint magnitude=value<0 ? 0u-asuint(value) : asuint(value);
    uint remainder=magnitude%size;
    return int(value<0 && remainder!=0 ? size-remainder : remainder);
}
uint textureIndex(Triangle t, int2 position, uint level) {
    uint colour = texels[t.texture.x + wrap(position.y, t.texture.z) * t.texture.y + wrap(position.x, t.texture.y)];
    if (t.shade.y != 0 && (t.texture.w & 3))
        colour = texels[t.shade.x + level * t.shade.y + min(colour, t.shade.y - 1)];
    return colour;
}
float4 sampleColour(Triangle t, int2 position, uint level) {
    uint raw = texels[t.texture.x + wrap(position.y, t.texture.z) * t.texture.y + wrap(position.x, t.texture.y)];
    if (raw == 0 && !(t.texture.w & 0x10000)) return 0;
    return paletteColour(textureIndex(t, position, level));
}
bool colourAt(Triangle t, float4 varying, uint4 fixed, out uint index, out float4 rgba) {
    if(t.scanY.w >= 0 && !filterMode) {
        index=(fixed.y >> 16) & 255;
        if(t.texture.y != 0) {
            int2 uv=int2(asint(fixed.z) >> 16, asint(fixed.w) >> 16);
            uint raw=texels[t.texture.x + wrap(uv.y,t.texture.z)*t.texture.y + wrap(uv.x,t.texture.y)];
            if(raw == 0 && !(t.texture.w & 0x10000)) { rgba=0; return false; }
            uint level=min(index,max(t.shade.z,1u)-1);
            index=textureIndex(t,uv,level);
        }
        rgba=paletteColour(index);
        return true;
    }
    index = uint(clamp(floor(varying.z), 0.0, 255.0));
    rgba = paletteColour(index);
    if (t.texture.y == 0) return true;
    float2 uv = varying.xy;
    if (t.texture.w & 0x20) uv /= max(varying.w, 1e-20);
    uint level = uint(clamp(floor(varying.z), 0.0, float(max(t.shade.z, 1u) - 1)));
    uint raw = texels[t.texture.x + wrap(int(floor(uv.y)), t.texture.z) * t.texture.y + wrap(int(floor(uv.x)), t.texture.y)];
    index = textureIndex(t, int2(floor(uv)), level);
    if (!filterMode) {
        if (raw == 0 && !(t.texture.w & 0x10000)) return false;
        rgba = paletteColour(index);
        return true;
    }
    float2 p = uv - 0.5;
    int2 base = int2(floor(p));
    float2 f = frac(p);
    rgba = lerp(lerp(sampleColour(t, base, level), sampleColour(t, base + int2(1,0), level), f.x),
                lerp(sampleColour(t, base + int2(0,1), level), sampleColour(t, base + int2(1,1), level), f.x), f.y);
    return rgba.a > 0;
}
float forwardDepth(float z) {
    float signedZ = z >= 32768 ? z - 65536 : z;
    float ndc = signedZ / 32768;
    if (orthographic) return max(nearClip, nearClip + ndc * (farClip - nearClip));
    float scale = (farClip + nearClip) / max(farClip - nearClip, 1e-6);
    float offset = 2 * farClip * nearClip / max(farClip - nearClip, 1e-6);
    return max(nearClip, offset / max(scale - ndc, 1e-6));
}
float3 cameraPoint(float4 vertex) {
    float d = forwardDepth(vertex.z);
    float2 xy = vertex.xy / float2(width,height) * float2(2,-2) + float2(-1,1);
    return orthographic ? float3(xy * float2(orthoHeight * width / height, orthoHeight) * 0.5, -d)
                        : float3(xy * float2(tangentX,tangentY) * d, -d);
}
float edgeDistance(Triangle t, float2 pixelPosition) {
    float result = 1e20;
    for (int i=0; i<3; ++i) {
        float2 a=t.position[i].xy, b=t.position[(i+1)%3].xy;
        float2 ab=b-a, ap=pixelPosition-a;
        result=min(result, abs(ab.x*ap.y-ab.y*ap.x)/max(length(ab),1e-6));
    }
    return result;
}
bool displayColour(Triangle t, float2 pixelPosition, float z, float4 varying, uint4 fixed, out uint index, out float4 rgba) {
    if (!colourAt(t,varying,fixed,index,rgba)) return false;
    float edge = mode == 4 || overlay ? edgeDistance(t,pixelPosition) : 1e20;
    if (mode == 4) {
        if (edge > wireWidth) return false;
        rgba=float4(0.47,0.8,0.77,1);
    } else if (mode == 5) {
        float level=1-saturate((forwardDepth(z)-nearClip)/max(farClip-nearClip,1e-6));
        rgba=float4(level,level,level,1);
    } else if (mode == 6) {
        float3 normal=cross(cameraPoint(t.position[1])-cameraPoint(t.position[0]),
                            cameraPoint(t.position[2])-cameraPoint(t.position[0]));
        normal=normalize(normal + float3(0,0,1e-20));
        if (normal.z < 0) normal=-normal;
        rgba=float4(normal*0.5+0.5,1);
    } else if (overlay && edge < wireWidth) rgba=float4(0.47,0.8,0.77,1);
    return true;
}
float4 backgroundColour(uint value) {
    if (mode == 5 && (value >> 8) != 65535) {
        float level=1-saturate((forwardDepth(float(value >> 8))-nearClip)/max(farClip-nearClip,1e-6));
        return float4(level,level,level,1);
    }
    if (mode == 1 || mode >= 4) return float4(0,0,0,1);
    float4 rgba=paletteColour(value & 255);
    rgba.a=opaqueBackground || (value >> 8) != 65535 ? 1 : 0;
    rgba.rgb *= rgba.a;
    return rgba;
}
#endif

#if !defined(VERTEX)
float edge(float2 a,float2 b) { return a.x*b.y-a.y*b.x; }
int scanCarries(int rows,int gradient) {
    // The edge starts at half a pixel. This bounded product retains every
    // carry without requiring 64-bit integer support in the shader.
    return int((32768u+uint(rows)*(uint(gradient)&65535u)) >> 16);
}
int scanX(int start,int gradient,int rows) {
    return start+rows*(gradient >> 16)+scanCarries(rows,gradient);
}
bool sampleAt(Triangle t,float2 pixelPosition,out float preciseZ,out float4 varying,out uint4 fixed) {
    preciseZ=0; varying=0; fixed=0;
    if(t.scanY.w >= 0) {
        int2 pixel=int2(floor(pixelPosition));
        if(!t.scanY.w || pixel.y<t.scanY.x || pixel.y>=t.scanY.z) return false;
        int rows=pixel.y-t.scanY.x;
        int carries=scanCarries(rows,t.scanEdges.y);
        int longX=scanX(t.scanEdges.x,t.scanEdges.y,rows);
        int minorX=pixel.y<t.scanY.y ? scanX(t.scanEdges.x,t.scanEdges.w,rows) :
            scanX(t.scanEdges.z,t.scanBottom.x,pixel.y-t.scanY.y);
        int first=(t.scanY.w&2)?longX:minorX, end=(t.scanY.w&2)?minorX:longX;
        if(pixel.x<first || pixel.x>=end) return false;
        for(int i=0;i<4;++i) {
            uint4 p=t.scanParameter[i];
            fixed[i]=p.x+uint(rows)*p.z+uint(carries)*p.y+uint(pixel.x-longX)*p.y;
        }
        preciseZ=float(fixed.x >> 16);
        varying=float4(float(asint(fixed.z))/65536.0,float(asint(fixed.w))/65536.0,
                       float(asint(fixed.y))/65536.0,1);
        return true;
    }
    float2 ab=t.position[1].xy-t.position[0].xy, ac=t.position[2].xy-t.position[0].xy;
    float area=edge(ab,ac);
    if(abs(area)<1e-8) return false;
    float2 ap=pixelPosition-t.position[0].xy;
    float b=edge(ap,ac)/area, c=edge(ab,ap)/area;
    float signArea=area>0?1:-1;
    float2 edges[3]={t.position[2].xy-t.position[1].xy,-ac,ab};
    float values[3]={edge(edges[0],pixelPosition-t.position[1].xy),edge(edges[1],pixelPosition-t.position[2].xy),edge(ab,ap)};
    for(int e=0;e<3;++e) {
        float2 d=edges[e]*signArea;
        bool inclusive=d.y<0||(d.y==0&&d.x>0);
        float atEdge=values[e]*signArea;
        if(atEdge<0||(atEdge==0&&!inclusive)) return false;
    }
    preciseZ=t.position[0].z+b*(t.position[1].z-t.position[0].z)+c*(t.position[2].z-t.position[0].z);
    varying=t.varying[0]+b*(t.varying[1]-t.varying[0])+c*(t.varying[2]-t.varying[0]);
    varying.w=t.position[0].w+b*(t.position[1].w-t.position[0].w)+c*(t.position[2].w-t.position[0].w);
    return true;
}
#endif
#if defined(COMPUTE)
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    uint pixel=id.y*width+id.x;
    uint value=output[pixel];
    float4 rgba=backgroundColour(value);
    uint tile=(id.y/16)*tilesX+id.x/16;
    float2 pixelPosition=float2(id.xy)+0.5;
    for(uint k=bins[tile]; k<bins[tile+1]; ++k) {
        Triangle t=triangles[bins[k]];
        float preciseZ; float4 v; uint4 fixed;
        if(!sampleAt(t,pixelPosition,preciseZ,v,fixed)) continue;
        uint z=uint(clamp(floor(preciseZ),0.0,65535.0));
        if(z>(value>>8) || (t.texture.y==0 && z==(value>>8))) continue;
        uint colour; float4 sampleRGBA;
        if(displayColour(t,pixelPosition,preciseZ,v,fixed,colour,sampleRGBA)) {
            value=(z<<8)|colour;
            rgba=sampleRGBA+rgba*(1-sampleRGBA.a);
        }
    }
    output[pixel]=value;
    output[pixel+width*height]=pack(rgba);
}
#else
struct Interpolants {
    float4 position : SV_Position;
    nointerpolation uint triangleIndex : TEXCOORD2;
};
#if defined(VERTEX)
Interpolants main(uint vertex : SV_VertexID) {
    Interpolants o=(Interpolants)0;
    if(background) {
        float2 xy=float2((vertex<<1)&2,vertex&2);
        o.position=float4(xy*float2(2,-2)+float2(-1,1),0,1);
        return o;
    }
    uint i=vertex/6+firstTriangle, corner=vertex%6;
    Triangle t=triangles[i];
    float2 lo=floor(min(t.position[0].xy,min(t.position[1].xy,t.position[2].xy)))-1;
    float2 hi=floor(max(t.position[0].xy,max(t.position[1].xy,t.position[2].xy)))+2;
    uint2 corners[6]={uint2(0,0),uint2(1,0),uint2(0,1),uint2(0,1),uint2(1,0),uint2(1,1)};
    float2 p=lerp(lo,hi,float2(corners[corner]));
    o.position=float4(p/float2(width,height)*float2(2,-2)+float2(-1,1),0,1);
    o.triangleIndex=i;
    return o;
}
#else
struct Fragment { uint colour : SV_Target0; float4 rgba : SV_Target1; float depth : SV_Depth; };
Fragment main(Interpolants input) {
    Fragment o;
    if(background) {
        o.colour=seed[uint(input.position.y)*width+uint(input.position.x)];
        o.depth=float(o.colour>>8)/65535.0;
        o.rgba=backgroundColour(o.colour);
        return o;
    }
    Triangle t=triangles[input.triangleIndex];
    float preciseZ; float4 varying; uint4 fixed;
    if(!sampleAt(t,input.position.xy,preciseZ,varying,fixed)) discard;
    uint colour;
    if(!displayColour(t,input.position.xy,preciseZ,varying,fixed,colour,o.rgba)) discard;
    uint z=uint(clamp(floor(preciseZ),0.0,65535.0));
    o.colour=(z<<8)|colour; o.depth=float(z)/65535.0;
    return o;
}
#endif
#endif
