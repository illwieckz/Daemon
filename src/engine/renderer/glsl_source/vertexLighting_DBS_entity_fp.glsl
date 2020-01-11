/*
===========================================================================
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

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

/* vertexLighting_DBS_entity_fp.glsl */

uniform sampler2D	u_DiffuseMap;
uniform sampler2D	u_MaterialMap;
uniform sampler2D	u_GlowMap;

uniform samplerCube	u_EnvironmentMap0;
uniform samplerCube	u_EnvironmentMap1;
uniform float		u_EnvironmentInterpolation;

uniform float		u_AlphaThreshold;
uniform vec3		u_ViewOrigin;

uniform sampler3D       u_LightGrid1;
uniform sampler3D       u_LightGrid2;
uniform vec3            u_LightGridOrigin;
uniform vec3            u_LightGridScale;

IN(smooth) vec3		var_Position;
IN(smooth) vec2		var_TexCoords;
IN(smooth) vec4		var_Color;
IN(smooth) vec3		var_Tangent;
IN(smooth) vec3		var_Binormal;
IN(smooth) vec3		var_Normal;

DECLARE_OUTPUT(vec4)

void ReadLightGrid(in vec3 pos, out vec3 lgtDir,
		   out vec3 ambCol, out vec3 lgtCol ) {
	vec4 texel1 = texture3D(u_LightGrid1, pos);
	vec4 texel2 = texture3D(u_LightGrid2, pos);

	ambCol = texel1.xyz;
	lgtCol = texel2.xyz;

	lgtDir.x = (texel1.w * 255.0 - 128.0) / 127.0;
	lgtDir.y = (texel2.w * 255.0 - 128.0) / 127.0;
	lgtDir.z = 1.0 - abs( lgtDir.x ) - abs( lgtDir.y );

	vec2 signs = 2.0 * step( 0.0, lgtDir.xy ) - vec2( 1.0 );
	if( lgtDir.z < 0.0 ) {
		lgtDir.xy = signs * ( vec2( 1.0 ) - abs( lgtDir.yx ) );
	}

	lgtDir = normalize( lgtDir );
}

void	main()
{
	// compute light direction in world space
	vec3 L;
	vec3 ambCol;
	vec3 lgtCol;

	ReadLightGrid( (var_Position - u_LightGridOrigin) * u_LightGridScale,
		       L, ambCol, lgtCol );

	// compute view direction in world space
	vec3 viewDir = normalize(u_ViewOrigin - var_Position);

	vec2 texCoords = var_TexCoords;

	mat3 tangentToWorldMatrix = mat3(var_Tangent.xyz, var_Binormal.xyz, var_Normal.xyz);

#if defined(USE_PARALLAX_MAPPING)
	// compute texcoords offset from heightmap
	vec2 texOffset = ParallaxTexOffset(texCoords, viewDir, tangentToWorldMatrix);

	texCoords += texOffset;
#endif // USE_PARALLAX_MAPPING

	// compute normal in world space from normalmap
	vec3 normal = NormalInWorldSpace(texCoords, tangentToWorldMatrix);

#if defined(USE_REFLECTIVE_SPECULAR)
	// not implemented for PBR yet

	vec4 material = texture2D(u_MaterialMap, texCoords).rgba;

	vec4 envColor0 = textureCube(u_EnvironmentMap0, reflect(-viewDir, normal)).rgba;
	vec4 envColor1 = textureCube(u_EnvironmentMap1, reflect(-viewDir, normal)).rgba;

	material.rgb *= mix(envColor0, envColor1, u_EnvironmentInterpolation).rgb;

#else // USE_REFLECTIVE_SPECULAR
	// simple Blinn-Phong
	vec4 material = texture2D(u_MaterialMap, texCoords).rgba;

#endif // USE_REFLECTIVE_SPECULAR

	// compute the diffuse term
	vec4 diffuse = texture2D(u_DiffuseMap, texCoords) * var_Color;

	diffuse.rgb = fromSRGB(diffuse.rgb);

	if( abs(diffuse.a + u_AlphaThreshold) <= 1.0 )
	{
		discard;
		return;
	}

// add Rim Lighting to highlight the edges
#if defined(r_RimLighting)
	float rim = pow(1.0 - clamp(dot(normal, viewDir), 0.0, 1.0), r_RimExponent);
	vec3 emission = ambCol * rim * rim * 0.2;
#endif

	// compute final color
	vec4 color = vec4(ambCol * r_AmbientScale * diffuse.xyz, diffuse.a);
	computeLight( L, normal, viewDir, lgtCol, diffuse, material, color );

	computeDLights( var_Position, normal, viewDir, diffuse, material, color );

#if defined(r_RimLighting)
	color.rgb += 0.7 * emission;
#endif

#if defined(r_glowMapping)
	color.rgb += texture2D(u_GlowMap, texCoords).rgb;
#endif // r_glowMapping

	outputColor = color;

// Debugging
#if defined(r_showNormalMaps)
	// convert normal to [0,1] color space
	normal = normal * 0.5 + 0.5;
	outputColor = vec4(normal, 1.0);
#elif defined(r_showMaterialMaps)
	outputColor = material;
#endif
}
