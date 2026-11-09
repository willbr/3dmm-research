// Indexed BRender rasterization shared by the compute and graphics backends.
// Vertex coordinates and intensities have already been lit and clipped.
struct Triangle
{
    float4 position[3]; // pixel X/Y, unsigned depth 0..65535, reciprocal W
    float4 varying[3];  // U, V, palette intensity, unused
    uint4 texture;     // offset, width, height, material flags
    uint4 shade;       // offset, width, height, unused
};

#if defined(COMPUTE)
StructuredBuffer<Triangle> triangles : register(t0, space0);
StructuredBuffer<uint> texels : register(t1, space0);
StructuredBuffer<uint> bins : register(t2, space0);
RWStructuredBuffer<uint> output : register(u0, space1);
cbuffer Parameters : register(b0, space2) { uint width, height, tilesX, background; };
#elif defined(VERTEX)
StructuredBuffer<Triangle> triangles : register(t0, space0);
cbuffer Parameters : register(b0, space1) { uint width, height, tilesX, background; };
#else
StructuredBuffer<Triangle> triangles : register(t0, space2);
StructuredBuffer<uint> texels : register(t1, space2);
StructuredBuffer<uint> seed : register(t2, space2);
cbuffer Parameters : register(b0, space3) { uint width, height, tilesX, background; };
#endif

#if !defined(VERTEX)
int wrap(int value, uint size) { return (value % int(size) + int(size)) % int(size); }
bool colourAt(Triangle t, float4 varying, out uint colour)
{
    colour = uint(clamp(floor(varying.z), 0.0, 255.0));
    if (t.texture.y == 0) return true;
    float2 uv = varying.xy;
    if (t.texture.w & 0x20) uv /= max(varying.w, 1e-20);
    int x = wrap(int(floor(uv.x)), t.texture.y);
    int y = wrap(int(floor(uv.y)), t.texture.z);
    colour = texels[t.texture.x + y * t.texture.y + x];
    if (colour == 0 && !(t.texture.w & 0x10000)) return false;
    if (t.shade.y != 0 && (t.texture.w & 3))
    {
        uint level = uint(clamp(floor(varying.z), 0.0, float(t.shade.z - 1)));
        colour = texels[t.shade.x + level * t.shade.y + min(colour, t.shade.y - 1)];
    }
    return true;
}
#endif

#if defined(COMPUTE)
float edge(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    uint pixel = id.y * width + id.x;
    uint value = output[pixel];
    uint tile = (id.y / 16) * tilesX + id.x / 16;
    float2 samplePosition = float2(id.xy) + 0.5;
    for (uint k = bins[tile]; k < bins[tile + 1]; ++k)
    {
        Triangle t = triangles[bins[k]];
        float2 ab = t.position[1].xy - t.position[0].xy;
        float2 ac = t.position[2].xy - t.position[0].xy;
        float area = edge(ab, ac);
        if (abs(area) < 1e-8) continue;
        float2 ap = samplePosition - t.position[0].xy;
        float b = edge(ap, ac) / area, c = edge(ab, ap) / area;
        // Shared edges belong to one triangle, using a top-left rule.
        float signArea = area > 0 ? 1 : -1;
        float2 edges[3] = {t.position[2].xy - t.position[1].xy, -ac, ab};
        float edgeValues[3] = {edge(edges[0], samplePosition - t.position[1].xy),
                              edge(edges[1], samplePosition - t.position[2].xy), edge(ab, ap)};
        bool inside = true;
        for (int e = 0; e < 3; ++e)
        {
            float2 d = edges[e] * signArea;
            bool inclusive = d.y < 0 || (d.y == 0 && d.x > 0);
            float valueAtEdge = edgeValues[e] * signArea;
            if (valueAtEdge < 0 || (valueAtEdge == 0 && !inclusive)) inside = false;
        }
        if (!inside) continue;
        uint z = uint(clamp(floor(t.position[0].z + b*(t.position[1].z-t.position[0].z) + c*(t.position[2].z-t.position[0].z)), 0.0, 65535.0));
        if (z > (value >> 8)) continue;
        float4 v = t.varying[0] + b*(t.varying[1]-t.varying[0]) + c*(t.varying[2]-t.varying[0]);
        v.w = t.position[0].w + b*(t.position[1].w-t.position[0].w) + c*(t.position[2].w-t.position[0].w);
        uint colour;
        if (colourAt(t, v, colour)) value = (z << 8) | colour;
    }
    output[pixel] = value;
}
#else
struct Interpolants
{
    float4 position : SV_Position;
    noperspective float4 varying : TEXCOORD0;
    noperspective float2 depthQ : TEXCOORD1;
    nointerpolation uint triangleIndex : TEXCOORD2;
};
#if defined(VERTEX)
Interpolants main(uint vertex : SV_VertexID)
{
    Interpolants o = (Interpolants)0;
    if (background)
    {
        float2 xy = float2((vertex << 1) & 2, vertex & 2);
        o.position = float4(xy * float2(2, -2) + float2(-1, 1), 0, 1);
        return o;
    }
    uint i = vertex / 3, corner = vertex % 3;
    Triangle t = triangles[i];
    float4 p = t.position[corner];
    o.position = float4(p.xy / float2(width,height) * float2(2,-2) + float2(-1,1), p.z / 65535.0, 1);
    o.varying = t.varying[corner];
    o.depthQ = p.zw;
    o.triangleIndex = i;
    return o;
}
#else
struct Fragment { uint colour : SV_Target0; float depth : SV_Depth; };
Fragment main(Interpolants input)
{
    Fragment o;
    if (background)
    {
        o.colour = seed[uint(input.position.y) * width + uint(input.position.x)];
        o.depth = float(o.colour >> 8) / 65535.0;
        return o;
    }
    Triangle t = triangles[input.triangleIndex];
    float4 varying = input.varying;
    varying.w = input.depthQ.y;
    uint colour;
    if (!colourAt(t, varying, colour)) discard;
    uint z = uint(clamp(floor(input.depthQ.x), 0.0, 65535.0));
    o.colour = (z << 8) | colour;
    o.depth = float(z) / 65535.0;
    return o;
}
#endif
#endif
