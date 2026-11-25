// Shift Happens: voter node SDF. Body of a Custom material node; returns float4(color, coverage), non-premultiplied.
// Inputs (world units = design pixels; screen right = +Y world, screen up = +X world for the top-down camera):
//   PW  float2  pixel world XY            CW  float2  node centre world XY      Rad float   body radius (world units)
//   Fill float3 owner colour              Conv float  conviction 0..1           Lock float  immunity 0..1
//   Gate float  <0 none, 0..1 progress    Hover float 0..1                      Flash float 0..1 (flip pulse)
//   Role float  0 member 1 hub 2 celeb 3 bridge   Expr float -1..1 (frown..smile)   Sock float 0/1
//   Pressure float 0..1 (the voter is about to flip back: it shakes and a ring pulses in the colour of the entry pushing it)
//   Threat float 0..1 (an announced attack on this voter: 1 just announced .. 0 lands now; a ring closes in), PushCol float3 colour of the pusher / attacker
//   Shield float 0..1 (a Rally shield: a teal dome and ring while it lasts)
//   Night float 0..1   Tm float seconds   Heat float 0..1   Cur float2 cursor world XY
//   Solid float 0..1 (1 in the see-through window: the desktop is behind the voter, so every part of it is made solid)
// q-space: x right, y down, body radius = 1.

float2 dW = PW - CW;
float2 q = float2(dW.y, -dW.x) / max(Rad, 1.0);
float aa = max(max(fwidth(q.x), fwidth(q.y)), 1e-4);
q.x += 0.05 * Pressure * sin(Tm * 34.0);   // a voter under pressure shakes

float swell = 1.0 + 0.08 * Hover + 0.22 * Flash;
float rr = length(q);
float2 aq = abs(q);
float isSock = step(0.5, Sock);
float dBody = lerp(rr - swell, max(aq.x, aq.y) - 0.84 * swell, isSock);

float isHub = step(0.5, Role) * step(Role, 1.5);
float isCel = step(1.5, Role) * step(Role, 2.5);
float isBri = step(2.5, Role);

float3 ink = lerp(float3(0.10, 0.09, 0.17), float3(0.94, 0.93, 1.00), Night);
float hdr = 1.0 + 1.4 * Night;

float3 pm = float3(0.0, 0.0, 0.0);   // premultiplied colour
float al = 0.0;                      // coverage
float a = 0.0;
float d = 0.0;

float ang = atan2(q.x, -q.y);
float a01 = frac(ang * 0.15915494 + 1.0);   // 0 at the top, clockwise

// Rings are laid out in screen pixels from the body edge, not in body radii: a member is only about 11 px across, so radius-relative rings were 1.4 px thin.
// Every ring is a dark outline band with the coloured fill inside it, so it reads on pale paper and on the night sky alike.
float px = 1.0 / max(Rad, 1.0);   // one screen pixel in q units
float edgeOut = 1.12;             // just outside the body outline

// 1. conviction ring: ink outline, dim track, owner-coloured arc (centre 5 px out, 3.6 px wide)
float rConv = edgeOut + 5.0 * px;
d = abs(rr - rConv) - 2.3 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * 0.92;
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
d = abs(rr - rConv) - 1.6 * px;
float ringConv = 1.0 - smoothstep(-aa, aa, d);
a = ringConv * 0.85;
pm = lerp(float3(0.78, 0.78, 0.84), float3(0.16, 0.16, 0.24), Night) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
a = ringConv * step(a01, saturate(Conv)) * step(0.001, Conv);
pm = (Fill * 1.15 + 0.10) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 2. immunity dashes (10.5 px out)
d = abs(rr - (edgeOut + 10.5 * px)) - 1.5 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * step(0.5, frac(a01 * 30.0 - Tm * 0.7)) * saturate(Lock * 5.0);
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 3. gate ring (closed gates only), 16 px out: ink outline, dark track, amber progress arc
float gateOn = step(0.0, Gate) * step(Gate, 1.5);
float rGate = edgeOut + 16.0 * px;
d = abs(rr - rGate) - 3.3 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * 0.92 * gateOn;
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
d = abs(rr - rGate) - 2.4 * px;
float ringGate = (1.0 - smoothstep(-aa, aa, d)) * gateOn;
a = ringGate * 0.85;
pm = float3(0.30, 0.27, 0.33) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
a = ringGate * step(a01, saturate(Gate)) * step(0.001, Gate);
pm = float3(1.0, 0.80, 0.15) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 3b. Rally shield: a translucent teal dome behind the body and a solid ring at its edge, for as long as the shield lasts
float shieldOn = step(0.001, Shield);
float rShield = edgeOut + 13.0 * px;
d = rr - rShield;
a = (1.0 - smoothstep(-aa, aa, d)) * 0.20 * shieldOn * saturate(Shield * 3.0);
pm = float3(0.20, 0.85, 0.75) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
d = abs(rr - rShield) - 3.0 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * 0.92 * shieldOn;
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
d = abs(rr - rShield) - 1.9 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * shieldOn * (0.55 + 0.45 * saturate(Shield * 2.0));
pm = float3(0.20, 0.90, 0.78) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 4. body, outline, hub ring
float aB = 1.0 - smoothstep(-aa, aa, dBody);
float3 body = Fill * (0.90 + 0.14 * saturate(-q.y));
pm = body * aB + pm * (1.0 - aB);
al = aB + al * (1.0 - aB);
d = abs(dBody + 0.05) - 0.05;
a = (1.0 - smoothstep(-aa, aa, d)) * 0.95;
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
d = abs(dBody - 0.16) - 0.035;
a = (1.0 - smoothstep(-aa, aa, d)) * 0.80 * isHub;
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 5. face (not for sockpuppets, which get a visor)
float faceOn = (1.0 - isSock) * aB;
float2 e1 = float2(-0.36, -0.14);
float2 e2 = float2(0.36, -0.14);
float dE = min(length(q - e1), length(q - e2)) - 0.21;
a = (1.0 - smoothstep(-aa, aa, dE)) * faceOn;
pm = float3(1.0, 1.0, 1.0) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
float2 L = Cur - CW;
float2 ls = normalize(float2(L.y, -L.x) + float2(1e-4, 1e-4)) * 0.085;
float dP = min(length(q - e1 - ls), length(q - e2 - ls)) - 0.105;
a = (1.0 - smoothstep(-aa, aa, dP)) * faceOn;
pm = float3(0.07, 0.06, 0.12) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
float ym = 0.40 + 0.17 * Expr * (1.0 - saturate(q.x * q.x / 0.2025));
float dMouth = max(abs(q.y - ym) * 0.85 - 0.045, abs(q.x) - 0.45);
a = (1.0 - smoothstep(-aa, aa, dMouth)) * faceOn;
pm = float3(0.07, 0.06, 0.12) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 5b. sockpuppet visor with glowing slits, and an antenna
float sockOn = isSock * aB;
float dV = max(abs(q.x) - 0.66, abs(q.y + 0.10) - 0.20);
a = (1.0 - smoothstep(-aa, aa, dV)) * sockOn;
pm = float3(0.04, 0.04, 0.08) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
float dSl = min(max(abs(q.x - 0.30) - 0.20, abs(q.y + 0.10) - 0.05), max(abs(q.x + 0.30) - 0.20, abs(q.y + 0.10) - 0.05));
a = (1.0 - smoothstep(-aa, aa, dSl)) * sockOn;
pm = (Fill * 2.0 + 0.5) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
float dAnt = min(max(abs(q.x) - 0.05, abs(q.y + 1.10) - 0.26), length(q - float2(0.0, -1.42)) - 0.12);
a = (1.0 - smoothstep(-aa, aa, dAnt)) * isSock;
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 6. celebrity crown and bridge diamond
float dCrown = min(min(max(abs(q.x) - 0.60, abs(q.y + 1.22) - 0.13), length(q - float2(-0.50, -1.50)) - 0.14),
                   min(length(q - float2(0.0, -1.58)) - 0.15, length(q - float2(0.50, -1.50)) - 0.14));
a = (1.0 - smoothstep(-aa, aa, dCrown)) * isCel;
pm = float3(1.0, 0.78, 0.18) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
float dDia = (abs(q.x) + abs(q.y + 1.25)) - 0.22;
a = (1.0 - smoothstep(-aa, aa, dDia)) * isBri;
pm = float3(0.55, 0.85, 1.0) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 7. flip flash ring
float rf = 1.1 + (1.0 - Flash) * 0.9;
d = abs(rr - rf) - 0.05;
a = (1.0 - smoothstep(-aa, aa, d)) * Flash * 0.9;
pm = (Fill * 1.3 + 0.2) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 8. pressure: a heavy orange alarm ring with an ink outline on a voter that is about to flip back (the whole node also shakes, see the top).
// It sits where the immunity dashes would be (a voter under pressure is never immune), drawn last so nothing covers it.
float pulse = 0.5 + 0.5 * sin(Tm * 9.0);
float pOn = saturate(Pressure * 2.0);
float rPress = edgeOut + 10.5 * px + 1.0 * px * pulse;
d = abs(rr - rPress) - 4.1 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * 0.95 * pOn;
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
d = abs(rr - rPress) - 2.9 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * pOn * (0.80 + 0.20 * pulse);
pm = (PushCol * 1.15 + 0.05) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

// 9. threat: an announced attack on this voter. A segmented ring in the attacker's colour closes in on the body and lands when it reaches it.
float tOn = step(0.001, Threat);
float rThreat = edgeOut + (5.0 + 12.0 * saturate(Threat)) * px;
float seg = step(0.5, frac(a01 * 8.0 - Tm * 0.8));
float tPulse = 0.75 + 0.25 * sin(Tm * 16.0) * (1.0 - saturate(Threat * 3.0));
d = abs(rr - rThreat) - 4.0 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * 0.95 * tOn;
pm = ink * a + pm * (1.0 - a);
al = a + al * (1.0 - a);
d = abs(rr - rThreat) - 2.7 * px;
a = (1.0 - smoothstep(-aa, aa, d)) * tOn * tPulse * (0.55 + 0.45 * seg);
pm = (PushCol * 1.2 + 0.08) * a + pm * (1.0 - a);
al = a + al * (1.0 - a);

float3 outc = pm / max(al, 1e-4);
// In the see-through window nothing of a voter may blend into the desktop behind it: the rings, outlines and tracks that are drawn at 0.8 to 0.95 become solid, and the soft dome a little
// more. It is a rescale, not a threshold, so every edge keeps its soft ramp and the anti-aliasing is as it was.
al = lerp(al, saturate(al / 0.86), Solid);
return float4(outc * hdr, al);
