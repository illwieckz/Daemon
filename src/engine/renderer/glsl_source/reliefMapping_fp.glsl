/*
===========================================================================
Copyright (C) 2009-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of XreaL source code.

XreaL source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

XreaL source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XreaL source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// reliefMapping_fp.glsl - Relief mapping helper functions

#if defined(r_normalMapping) || defined(USE_HEIGHTMAP_IN_NORMALMAP)
uniform sampler2D	u_NormalMap;
#endif // r_normalMapping || USE_HEIGHTMAP_IN_NORMALMAP

#if defined(r_normalMapping)
uniform vec3        u_NormalScale;
#endif // r_normalMapping

#if defined(USE_PARALLAX_MAPPING)
#if !defined(USE_HEIGHTMAP_IN_NORMALMAP)
uniform sampler2D	u_HeightMap;
#endif // !USE_HEIGHTMAP_IN_NORMALMAP
uniform float       u_ParallaxDepthScale;
uniform float       u_ParallaxOffsetBias;
#endif // USE_PARALLAX_MAPPING

// compute normal in tangent space
vec3 NormalInTangentSpace(vec2 texNormal)
{
	vec3 normal;

#if defined(r_normalMapping)
#if defined(USE_HEIGHTMAP_IN_NORMALMAP)
	// alpha channel contains the height map so do not try to reconstruct normal map from it
	normal = texture2D(u_NormalMap, texNormal).rgb;
	normal = 2.0 * normal - 1.0;
#else // !USE_HEIGHTMAP_IN_NORMALMAP
	// the Capcom trick abusing alpha channel of DXT1/5 formats to encode normal map
	// https://github.com/DaemonEngine/Daemon/issues/183#issuecomment-473691252
	//
	// the algorithm also works with normal maps in rgb format without alpha channel
	// but we still must be sure there is no height map in alpha channel hence the test
	//
	// crunch -dxn seems to produce such files, since alpha channel is abused such format
	// is unsuitable to embed height map, then height map must be distributed as loose file
	normal = texture2D(u_NormalMap, texNormal).rga;
	normal.x *= normal.z;
	normal.xy = 2.0 * normal.xy - 1.0;
	// In a perfect world this code must be enough:
	// normal.z = sqrt(1.0 - dot(normal.xy, normal.xy));
	//
	// Unvanquished texture known to trigger black normalmap artifacts
	// when doing Z reconstruction:
	//   textures/shared_pk02_src/rock01_n
	//
	// Although the normal vector is supposed to have a length of 1,
	// dot(normal.xy, normal.xy) may be greater than 1 due to compression
	// artifacts: values as large as 1.27 have been observed with crunch -dxn.
	// https://github.com/DaemonEngine/Daemon/pull/260#issuecomment-571010935
	//
	// This might happen with other formats too. So we must take care not to
	// take the square root of a negative number here.
	normal.z = sqrt(max(0, 1.0 - dot(normal.xy, normal.xy)));
#endif // !USE_HEIGHTMAP_IN_NORMALMAP
#if r_showNormalMaps != 4
	// HACK: 0 normal Z channel can't be good
	if (u_NormalScale.z != 0)
	{
		normal *= u_NormalScale;
	}
#endif // r_showNormalMaps != 4
#else // !r_normalMapping
	normal = vec3(0.5, 0.5, 1.0);
#endif // !r_normalMapping

	return normal;
}

// compute normal in worldspace from normalmap
vec3 NormalInWorldSpace(vec2 texNormal, mat3 tangentToWorldMatrix)
{
	// compute normal in tangent space from normalmap
	vec3 normal = NormalInTangentSpace(texNormal);
	// transform normal into world space

#if r_showNormalMaps == 3 || r_showNormalMaps == 4
	return normal;
#else // r_showNormalMaps != 3 &&  r_showNormalMaps != 4
	return normalize(tangentToWorldMatrix * normal);
#endif // r_showNormalMaps != 3 &&  r_showNormalMaps != 4
}

#if defined(USE_PARALLAX_MAPPING)
// compute texcoords offset from heightmap
// most of the code doing somewhat the same is likely to be named
// RayIntersectDisplaceMap in other id tech3-based engines
// so please keep the comment above to enable cross-tree look-up
vec2 ParallaxTexOffset(vec2 rayStartTexCoords, vec3 viewDir, mat3 tangentToWorldMatrix)
{
	// compute view direction in tangent space
	vec3 tangentViewDir = normalize(viewDir * tangentToWorldMatrix);

	vec2 displacement = tangentViewDir.xy * -u_ParallaxDepthScale / tangentViewDir.z;

	const int linearSearchSteps = 16;
	const int binarySearchSteps = 6;

	float depthStep = 1.0 / float(linearSearchSteps);
	float topDepth = 1.0 - u_ParallaxOffsetBias;

	// current size of search window
	float currentSize = depthStep;

	// current depth position
	float currentDepth = 0.0;

	// best match found (starts with last position 1.0)
	float bestDepth = 1.0;

	// search front to back for first point inside object
	for(int i = 0; i < linearSearchSteps - 1; ++i)
	{
		currentDepth += currentSize;

#if defined(USE_HEIGHTMAP_IN_NORMALMAP)
		float depth = texture2D(u_NormalMap, rayStartTexCoords + displacement * currentDepth).a;
#else // !USE_HEIGHTMAP_IN_NORMALMAP
		float depth = texture2D(u_HeightMap, rayStartTexCoords + displacement * currentDepth).g;
#endif // !USE_HEIGHTMAP_IN_NORMALMAP

		float heightMapDepth = topDepth - depth;

		if(bestDepth > 0.996) // if no depth found yet
		{
			if(currentDepth >= heightMapDepth)
			{
				bestDepth = currentDepth;
			}
		}
	}

	currentDepth = bestDepth;

	// recurse around first point (depth) for closest match
	for(int i = 0; i < binarySearchSteps; ++i)
	{
		currentSize *= 0.5;

#if defined(USE_HEIGHTMAP_IN_NORMALMAP)
		float depth = texture2D(u_NormalMap, rayStartTexCoords + displacement * currentDepth).a;
#else // !USE_HEIGHTMAP_IN_NORMALMAP
		float depth = texture2D(u_HeightMap, rayStartTexCoords + displacement * currentDepth).g;
#endif // !USE_HEIGHTMAP_IN_NORMALMAP

		float heightMapDepth = topDepth - depth;

		if(currentDepth >= heightMapDepth)
		{
			bestDepth = currentDepth;
			currentDepth -= 2.0 * currentSize;
		}

		currentDepth += currentSize;
	}

	return bestDepth * displacement;
}
#endif // USE_PARALLAX_MAPPING

#if r_showNormalMaps == 2
// Slow, should be used only for debugging
// All arguments should be in [0, 1]
// https://en.wikipedia.org/w/index.php?title=HSL_and_HSV&oldid=936097527#HSL_to_RGB
vec3 hsl2rgb(float H, float S, float L)
{
	float h = H * 6.0;
	float c = S * (1.0 - abs(2.0 * L - 1.0));
	float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));
	float m = L - c * 0.5;
	if (h < 1.0) return vec3(m + c, m + x, m);
	if (h < 2.0) return vec3(m + x, m + c, m);
	if (h < 3.0) return vec3(m, m + c, m + x);
	if (h < 4.0) return vec3(m, m + x, m + c);
	if (h < 5.0) return vec3(m + x, m, m + c);
	return vec3(m + c, m, m + x);
}

// For normal map debugging
vec3 NormalInTangentSpaceAsColor(vec2 texNormal)
{
	vec3 normal = NormalInTangentSpace(texNormal);
	if (normal.x == 0.0)
	{
		normal.x = 1e-30;
	}
	float hue = atan(normal.y, normal.x) / (2.0 * M_PI);
	if (hue < 0.0)
	{
		hue += 1.0;
	}
	float saturation = 1.0 - normal.z;
	return hsl2rgb(hue, saturation, 0.5);
}
#endif // r_showNormalMaps == 2
