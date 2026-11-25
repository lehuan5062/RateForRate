// Shift Happens: the see-through paper's colour scale per channel. Body of a Custom node; returns float3.
// Input: Op float, the paper's opacity 0..1.
// The desktop compositor wants the colour premultiplied in DISPLAY space (encoded colour x opacity, the normal blend of an image editor), but the engine premultiplies in linear light
// before the tonemapper and the gamma encode, so the paper must be pre-scaled by a curve that makes the DISPLAY colour come out at opacity x the opaque paper. This table was measured
// with Tools\glass_sim.ps1 (the headless see-through picture) on the game's own tonemapper, per channel (the filmic curve compresses red more than blue), at 5% steps of the opacity;
// the Substrate weight multiplies the colour by the opacity once more, which is included. To re-measure it after a change of the post process: sweep -ShiftGlassSim over 0.05 .. 1.0,
// and per channel move each entry until the display colour over a black desktop (_black.png) equals the opacity x the opaque paper (the 1.0 picture), as Docs/QA.md describes.
float3 T[21] = {
	float3(0.2096, 0.2392, 0.2753),
	float3(0.2096, 0.2392, 0.2753),
	float3(0.1950, 0.2145, 0.2431),
	float3(0.1964, 0.2138, 0.2430),
	float3(0.2030, 0.2195, 0.2503),
	float3(0.2118, 0.2275, 0.2604),
	float3(0.2229, 0.2379, 0.2718),
	float3(0.2341, 0.2502, 0.2841),
	float3(0.2454, 0.2630, 0.2985),
	float3(0.2592, 0.2763, 0.3141),
	float3(0.2742, 0.2912, 0.3310),
	float3(0.2906, 0.3093, 0.3496),
	float3(0.3093, 0.3294, 0.3697),
	float3(0.3318, 0.3520, 0.3963),
	float3(0.3603, 0.3801, 0.4267),
	float3(0.3947, 0.4147, 0.4634),
	float3(0.4409, 0.4619, 0.5112),
	float3(0.5059, 0.5268, 0.5768),
	float3(0.6022, 0.6262, 0.6706),
	float3(0.7572, 0.7749, 0.8050),
	float3(1.0000, 1.0000, 1.0000)
};
float t = saturate(Op) * 20.0;
int i = min((int)t, 19);
return lerp(T[i], T[i + 1], t - i);
