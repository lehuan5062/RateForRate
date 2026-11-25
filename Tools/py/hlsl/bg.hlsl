// Shift Happens: background. Body of a Custom node; returns the opaque emissive colour float3.
// Inputs: PW float2 pixel world XY, PaperA/PaperB float3 (day/night base colours), InkA/InkB float3 (day/night dot colours),
//         Night float 0..1, Tm float seconds, Beat float 0..1, Pat float (the jam's pattern: 0 dots, 1 pixel grid, 2 graph paper, 3 level rules).
float3 Paper = lerp(PaperA, PaperB, Night);
float3 Ink = lerp(InkA, InkB, Night);
float2 g = PW / 48.0;
float2 f = frac(g) - 0.5;
float dotr = length(f) - (0.040 + 0.012 * Beat);
float aa = max(fwidth(dotr), 1e-3);
float dots = 1.0 - smoothstep(-aa, aa, dotr);
if (Pat > 0.5)
{
    // the other jams' moods; the dots above stay exactly as they were for pattern 0
    float2 px = max(fwidth(PW), float2(1e-3, 1e-3));
    if (Pat < 1.5)
    {
        // pixel grid: 24-unit squares, a checker of faint cells and thin lines between them
        float2 q = PW / 24.0;
        float2 cell = floor(q);
        float checker = fmod(abs(cell.x + cell.y), 2.0);
        float2 e = (0.5 - abs(frac(q) - 0.5)) * 24.0;
        float2 l = 1.0 - smoothstep(0.4, 0.4 + px, e);
        dots = max(max(l.x, l.y) * 0.7, checker * 0.30);
    }
    else if (Pat < 2.5)
    {
        // graph paper: a fine line every 24 units, a bolder one every 120
        float2 q = PW / 24.0;
        float2 e = (0.5 - abs(frac(q) - 0.5)) * 24.0;
        float2 l = 1.0 - smoothstep(0.35, 0.35 + px, e);
        float2 Q = PW / 120.0;
        float2 E = (0.5 - abs(frac(Q) - 0.5)) * 120.0;
        float2 L = 1.0 - smoothstep(0.9, 0.9 + px, E);
        dots = max(max(l.x, l.y) * 0.45, max(L.x, L.y) * 0.95);
    }
    else
    {
        // level rules: horizontal lines every 48 units (a spirit level: everything on the same line), the one through the middle bolder
        float e = (0.5 - abs(frac(PW.x / 48.0) - 0.5)) * 48.0;
        float rule = 1.0 - smoothstep(0.55, 0.55 + px.x, e);
        float mid = 1.0 - smoothstep(1.4, 1.4 + px.x, abs(PW.x));
        dots = max(rule * 0.60, mid * 0.95);
    }
}
float r = min(length(PW) / 1000.0, 1.5);   // the window is not letterboxed any more: a wide or tall one shows paper far from the board, which must not go dark
float3 c = Paper * (1.0 - 0.14 * r * r);
float amt = (Pat > 0.5) ? lerp(0.20, 0.20, Night) : (0.10 + 0.10 * Night);   // the lines of the other moods are as strong by day as by night: the dots of the first jam are unchanged
c = lerp(c, Ink, dots * amt);
// slow drifting band so the field is not dead flat at night
float band = 0.5 + 0.5 * sin(Tm * 0.15 + PW.y * 0.004);
c *= 1.0 + 0.03 * band * Night;
return c;
