// Rate for Rate: soft halo behind a node. Body of a Custom node; returns premultiplied emissive float3 for an additive material.
// Inputs: PW float2 pixel world XY, CW float2 node centre world XY, Rad float body radius (world units), Fill float3,
//         Flash float 0..1, Hover float 0..1, Night float 0..1, Pow float overall strength.
float dist = length(PW - CW) / max(Rad, 1.0);          // 1.0 at the body edge
float fall = exp(-max(dist - 1.0, 0.0) * 1.9);          // soft exponential falloff outside the body
float inner = 1.0 - smoothstep(0.0, 1.0, dist);         // faint fill under the body so the edge glows
float edgeFade = 1.0 - smoothstep(2.2, 3.4, dist);      // quad is 3.4 radii wide: fade to zero before its border
float k = (0.16 + 0.22 * Night + 0.35 * Hover + 0.9 * Flash) * Pow;
return Fill * (fall * 0.6 + inner * 0.25) * edgeFade * k;
