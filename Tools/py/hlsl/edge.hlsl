// Shift Happens: curved influence edge with travelling pulses. Body of a Custom material node; returns float4(color, coverage).
// One quad per directed follow edge. The curve is a quadratic Bezier from P0 (influencer) to P1 (follower) whose control point is
// offset perpendicular to the chord by Bulge world units. Distance is taken to a 10-segment polyline, so there are no seams.
// Inputs: PW float2 pixel world XY, P0 float2, P1 float2, Bulge float, Col float3, Base float (line alpha), Pulse float (0..1),
//         Seed float, Speed float, Wid float (half width, world units), Night float, Tm float seconds.

float2 ab = P1 - P0;
float len = max(length(ab), 1e-3);
float2 dir = ab / len;
float2 nrm = float2(-dir.y, dir.x);
float2 ctrl = (P0 + P1) * 0.5 + nrm * Bulge;

float best = 1e9;
float bestT = 0.0;
float2 prev = P0;
[unroll]
for (int i = 1; i <= 10; i++)
{
    float t1 = i * 0.1;
    float u = 1.0 - t1;
    float2 cur = u * u * P0 + 2.0 * u * t1 * ctrl + t1 * t1 * P1;
    float2 seg = cur - prev;
    float sl = max(dot(seg, seg), 1e-5);
    float s = saturate(dot(PW - prev, seg) / sl);
    float dd = length(PW - (prev + seg * s));
    if (dd < best)
    {
        best = dd;
        bestT = (i - 1) * 0.1 + s * 0.1;
    }
    prev = cur;
}

float d = best - Wid;
float aa = max(fwidth(d), 1e-3);
float cov = 1.0 - smoothstep(-aa, aa, d);

// travelling pulses: about 2.5 per edge, moving from influencer (t = 0) to follower (t = 1)
float ph = frac(Tm * Speed - bestT * 2.5 + Seed);
float pulse = pow(saturate(1.0 - ph * 3.0), 2.0) * Pulse;
float3 col = Col * (0.60 + pulse * 2.4) * (1.0 + 0.8 * Night);
float dayBoost = 1.0 + 0.7 * (1.0 - Night);   // pale paper needs stronger lines than the night sky does
float alpha = cov * saturate(Base * dayBoost + pulse * 0.85);
return float4(col, alpha);
