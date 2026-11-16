// Shift Happens: background. Body of a Custom node; returns the opaque emissive colour float3.
// Inputs: PW float2 pixel world XY, PaperA/PaperB float3 (day/night base colours), InkA/InkB float3 (day/night dot colours),
//         Night float 0..1, Tm float seconds, Beat float 0..1.
float3 Paper = lerp(PaperA, PaperB, Night);
float3 Ink = lerp(InkA, InkB, Night);
float2 g = PW / 48.0;
float2 f = frac(g) - 0.5;
float dotr = length(f) - (0.040 + 0.012 * Beat);
float aa = max(fwidth(dotr), 1e-3);
float dots = 1.0 - smoothstep(-aa, aa, dotr);
float r = length(PW) / 1000.0;
float3 c = Paper * (1.0 - 0.14 * r * r);
c = lerp(c, Ink, dots * (0.10 + 0.10 * Night));
// slow drifting band so the field is not dead flat at night
float band = 0.5 + 0.5 * sin(Tm * 0.15 + PW.y * 0.004);
c *= 1.0 + 0.03 * band * Night;
return c;
