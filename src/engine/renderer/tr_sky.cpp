/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of Daemon source code.

Daemon source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Daemon source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_sky.c
#include "tr_local.h"
#include "gl_shader.h"

static const int SKY_SUBDIVISIONS = 8;
static const int HALF_SKY_SUBDIVISIONS = ( SKY_SUBDIVISIONS / 2 );

static float s_cloudTexCoords[ 6 ][ SKY_SUBDIVISIONS + 1 ][ SKY_SUBDIVISIONS + 1 ][ 2 ];
static float s_cloudTexP[ 6 ][ SKY_SUBDIVISIONS + 1 ][ SKY_SUBDIVISIONS + 1 ];

/*
===================================================================================

POLYGON TO BOX SIDE PROJECTION

===================================================================================
*/

static const vec3_t sky_clip[ 6 ] =
{
	{ 1,  1,  0 },
	{ 1,  -1, 0 },
	{ 0,  -1, 1 },
	{ 0,  1,  1 },
	{ 1,  0,  1 },
	{ -1, 0,  1 }
};

static float sky_mins[ 2 ][ 6 ], sky_maxs[ 2 ][ 6 ];
static float sky_min, sky_max;

/*
================
AddSkyPolygon
================
*/
static void AddSkyPolygon( int nump, vec3_t vecs )
{
	static const int vec_to_st[ 6 ][ 3 ] =
	{
		{ -2, 3,   1 },
		{ 2,  3,  -1 },

		{ 1,  3,   2 },
		{ -1, 3,  -2 },

		{ -2, -1,  3 },
		{ -2,  1, -3 }

		// { -1, 2,  3 },
		// { 1,  2, -3 }
	};

	float *lastVec = vecs + 3 * nump;
	vec3_t v, av;
	int axis;

	// Decide which face it maps to.
	VectorCopy( vec3_origin, v );

	for ( float *vec = vecs; vec < lastVec; vec += 3 )
	{
		VectorAdd( vec, v, v );
	}

	VectorSet( av, fabs( v[ 0 ] ), fabs( v[ 1 ] ), fabs( v[ 2 ] ) );

	if ( av[ 0 ] > av[ 1 ] && av[ 0 ] > av[ 2 ] )
	{
		axis = v[ 0 ] < 0 ? 1 : 0;
	}
	else if ( av[ 1 ] > av[ 2 ] && av[ 1 ] > av[ 0 ] )
	{
		axis = v[ 1 ] < 0 ? 3 : 2;
	}
	else
	{
		axis = v[ 2 ] < 0 ? 5 : 4;
	}

	// Project new texture coordinates.
	for ( float *vec = vecs; vec < lastVec; vec += 3 )
	{
		int i = vec_to_st[ axis ][ 2 ];

		float dv = i > 0 ? vec[ i - 1 ] : -vec[ -i - 1 ];

		if ( dv < 0.001 )
		{
			// Don't divide by zero.
			continue;
		}

		i = vec_to_st[ axis ][ 0 ];

		float s = i < 0 ? -vec[ -i - 1 ] / dv : vec[ i - 1 ] / dv;

		i = vec_to_st[ axis ][ 1 ];

		float t = i < 0 ? -vec[ -i - 1 ] / dv : vec[ i - 1 ] / dv;

		if ( s < sky_mins[ 0 ][ axis ] )
		{
			sky_mins[ 0 ][ axis ] = s;
		}

		if ( t < sky_mins[ 1 ][ axis ] )
		{
			sky_mins[ 1 ][ axis ] = t;
		}

		if ( s > sky_maxs[ 0 ][ axis ] )
		{
			sky_maxs[ 0 ][ axis ] = s;
		}

		if ( t > sky_maxs[ 1 ][ axis ] )
		{
			sky_maxs[ 1 ][ axis ] = t;
		}
	}
}

// Point on plane side epsilon.
static const float ON_EPSILON = 0.1f;

static const int MAX_CLIP_VERTS = 64;

/*
================
ClipSkyPolygon
================
*/
static void ClipSkyPolygon( int nump, vec3_t vecs, int stage )
{
	const float *norm;
	bool front, back;
	float *vec, *dist, dists[ MAX_CLIP_VERTS ];
	planeSide_t *side, sides[ MAX_CLIP_VERTS ];
	vec3_t newv[ 2 ][ MAX_CLIP_VERTS ];
	int newc[ 2 ];

	if ( nump > MAX_CLIP_VERTS - 2 )
	{
		Sys::Drop( "ClipSkyPolygon: MAX_CLIP_VERTS (%d)", MAX_CLIP_VERTS );
	}

	if ( stage == 6 )
	{
		// Fully clipped, so draw it.
		AddSkyPolygon( nump, vecs );
		return;
	}

	float *lastVec = vecs + 3 * nump;

	front = back = false;
	norm = sky_clip[ stage ];

	for ( vec = vecs, dist = dists, side = sides;
		vec < lastVec;
		vec += 3, dist++, side++ )
	{
		*dist = DotProduct( vec, norm );

		if ( *dist > ON_EPSILON )
		{
			front = true;
			*side = planeSide_t::SIDE_FRONT;
		}
		else if ( *dist < -ON_EPSILON )
		{
			back = true;
			*side = planeSide_t::SIDE_BACK;
		}
		else
		{
			*side = planeSide_t::SIDE_ON;
		}
	}

	if ( !front || !back )
	{
		// Not clipped.
		ClipSkyPolygon( nump, vecs, stage + 1 );
		return;
	}

	// Clip it.
	side[ 0 ] = sides[ 0 ];
	dist[ 0 ] = dists[ 0 ];
	newc[ 0 ] = newc[ 1 ] = 0;
	VectorCopy( vecs, lastVec );

	for ( vec = vecs, dist = dists, side = sides;
		vec < lastVec;
		vec += 3, dist++, side++ )
	{
		switch ( side[ 0 ] )
		{
			case planeSide_t::SIDE_CROSS:
				break;

			case planeSide_t::SIDE_FRONT:
				VectorCopy( vec, newv[ 0 ][ newc[ 0 ] ] );
				newc[ 0 ]++;
				break;

			case planeSide_t::SIDE_BACK:
				VectorCopy( vec, newv[ 1 ][ newc[ 1 ] ] );
				newc[ 1 ]++;
				break;

			case planeSide_t::SIDE_ON:
				VectorCopy( vec, newv[ 0 ][ newc[ 0 ] ] );
				newc[ 0 ]++;
				VectorCopy( vec, newv[ 1 ][ newc[ 1 ] ] );
				newc[ 1 ]++;
				break;
		}

		if ( side[ 0 ] == planeSide_t::SIDE_ON
			|| side[ 1 ] == planeSide_t::SIDE_ON
			|| side[ 0 ] == side[ 1 ] )
		{
			continue;
		}

		float d = dist[ 0 ] / ( dist[ 0 ] - dist[ 1 ] );

		vec3_t temp;
		float *nextvec = vec + 3;
		VectorSubtract( nextvec, vec, temp );
		VectorMA( vec, d, temp, temp );
		VectorCopy( temp, newv[ 0 ][ newc[ 0 ] ] );
		VectorCopy( temp, newv[ 1 ][ newc[ 1 ] ] );

		newc[ 0 ]++;
		newc[ 1 ]++;
	}

	// Continue.
	ClipSkyPolygon( newc[ 0 ], newv[ 0 ][ 0 ], stage + 1 );
	ClipSkyPolygon( newc[ 1 ], newv[ 1 ][ 0 ], stage + 1 );
}

/*
==============
ClearSkyBox
==============
*/
inline void ClearSkyBox()
{
	static const float mins[ 2 ][ 6 ] = {
		{ 9999, 9999, 9999, 9999, 9999, 9999 },
		{ 9999, 9999, 9999, 9999, 9999, 9999 }
	};

	static const float maxs[ 2 ][ 6 ] = {
		{ -9999, -9999, -9999, -9999, -9999, -9999 },
		{ -9999, -9999, -9999, -9999, -9999, -9999 }
	};

	memcpy( sky_mins, mins, sizeof( mins ) );
	memcpy( sky_maxs, maxs, sizeof( maxs ) );
}

/*
================
Tess_ClipSkyPolygons
================
*/
void Tess_ClipSkyPolygons()
{
	ClearSkyBox();

	glIndex_t *lastIndex = tess.indexes + tess.numIndexes;

	for ( glIndex_t *tessIndex = tess.indexes;
		tessIndex < lastIndex;
		tessIndex += 3 )
	{
		// Need one extra point for clipping.
		vec3_t p[ 5 ];

		VectorSubtract( tess.verts[ tessIndex[ 0 ] ].xyz, backEnd.viewParms.orientation.origin, p[ 0 ] );
		VectorSubtract( tess.verts[ tessIndex[ 1 ] ].xyz, backEnd.viewParms.orientation.origin, p[ 1 ] );
		VectorSubtract( tess.verts[ tessIndex[ 2 ] ].xyz, backEnd.viewParms.orientation.origin, p[ 2 ] );

		ClipSkyPolygon( 3, p[ 0 ], 0 );
	}
}

/*
===================================================================================

CLOUD VERTEX GENERATION

===================================================================================
*/

/*
================
MakeSkyVec

Parms: s, t range from -1 to 1.
================
*/
static void MakeSkyVec( float s, float t, int axis, vec2_t outSt, vec4_t outXYZ )
{
	// 1 = s, 2 = t, 3 = 2048
	static const int st_to_vec[ 6 ][ 3 ] =
	{
		{ 3,  -1, 2 },
		{ -3, 1,  2 },

		{ 1,  3,  2 },
		{ -1, -3, 2 },

		// 0 degrees yaw, look straight up.
		{ -2, -1, 3 },
		// Look straight down.
		{ 2,  -1, -3}
	};

	float boxSize = backEnd.viewParms.zFar / M_ROOT3;

	vec3_t b;
	VectorSet( b, s, t, 1);
	VectorScale( b, boxSize, b );

	int i = st_to_vec[ axis ][ 0 ];
	outXYZ[ 0 ] = ( i < 0 ) ? -b[ -i - 1 ] : b[ i - 1 ];
	i = st_to_vec[ axis ][ 1 ];
	outXYZ[ 1 ] = ( i < 0 ) ? -b[ -i - 1 ] : b[ i - 1 ];
	i = st_to_vec[ axis ][ 2 ];
	outXYZ[ 2 ] = ( i < 0 ) ? -b[ -i - 1 ] : b[ i - 1 ];

	outXYZ[ 3 ] = 1;

	// Avoid bilerp seam.
	s = ( s + 1 ) * 0.5F;
	t = ( t + 1 ) * 0.5F;

	if ( s < sky_min )
	{
		s = sky_min;
	}
	else if ( s > sky_max )
	{
		s = sky_max;
	}

	if ( t < sky_min )
	{
		t = sky_min;
	}
	else if ( t > sky_max )
	{
		t = sky_max;
	}

	t = 1.0F - t;

	if ( outSt )
	{
		outSt[ 0 ] = s;
		outSt[ 1 ] = t;
	}
}

static vec4_t s_skyPoints[ SKY_SUBDIVISIONS + 1 ][ SKY_SUBDIVISIONS + 1 ];
static vec2_t s_skyTexCoords[ SKY_SUBDIVISIONS + 1 ][ SKY_SUBDIVISIONS + 1 ];

static void FillCloudySkySide( const int mins[ 2 ], const int maxs[ 2 ], bool addIndexes )
{
	int vertexStart = tess.numVertexes;

	shaderVertex_t *tessVertex = tess.verts + tess.numVertexes;

	for ( int t = mins[ 1 ] + HALF_SKY_SUBDIVISIONS;
		t <= maxs[ 1 ] + HALF_SKY_SUBDIVISIONS;
		t++ )
	{
		for ( int s = mins[ 0 ] + HALF_SKY_SUBDIVISIONS;
			s <= maxs[ 0 ] + HALF_SKY_SUBDIVISIONS;
			s++, tessVertex++ )
		{
			VectorAdd( s_skyPoints[ t ][ s ], backEnd.viewParms.orientation.origin, tessVertex->xyz );

			Vector2Set( tessVertex->texCoords,
				floatToHalf( s_skyTexCoords[ t ][ s ][ 0 ] ),
				floatToHalf( s_skyTexCoords[ t ][ s ][ 1 ] ) );

			tess.numVertexes++;

			if ( tess.numVertexes >= SHADER_MAX_VERTEXES )
			{
				Sys::Drop( "SHADER_MAX_VERTEXES hit in FillCloudySkySide()" );
			}
		}
	}

	tess.attribsSet |= ATTR_POSITION | ATTR_TEXCOORD;

	glIndex_t *tessIndex = tess.indexes + tess.numIndexes;

	int tHeight = maxs[ 1 ] - mins[ 1 ] + 1;
	int sWidth = maxs[ 0 ] - mins[ 0 ] + 1;

	// Only add indexes for one pass, otherwise it would draw multiple times for each pass.
	if ( addIndexes )
	{
		for ( int t = 0;
			t < tHeight - 1;
			t++ )
		{
			for ( int s = 0;
				s < sWidth - 1;
				s++, tessIndex += 6 )
			{
				tessIndex[ 0 ] = vertexStart + s + t * ( sWidth );
				tessIndex[ 1 ] = vertexStart + s + ( t + 1 ) * ( sWidth );
				tessIndex[ 2 ] = vertexStart + s + 1 + t * ( sWidth );

				tessIndex[ 3 ] = tessIndex[ 1 ];
				tessIndex[ 4 ] = vertexStart + s + 1 + ( t + 1 ) * ( sWidth );
				tessIndex[ 5 ] = tessIndex[ 2 ];

				tess.numIndexes += 6;
			}
		}
	}
}

static void DrawSkyBox()
{
	sky_min = 0;
	sky_max = 1;

	Com_Memset( s_skyTexCoords, 0, sizeof( s_skyTexCoords ) );

	// Set up for drawing.
	tess.multiDrawPrimitives = 0;
	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.attribsSet = 0;

	GL_State( GLS_DEFAULT );

	Tess_MapVBOs( false );

	for ( int i = 0; i < 6; i++ )
	{
		int sky_mins_subd[ 2 ], sky_maxs_subd[ 2 ];
		int s, t;

		sky_mins[ 0 ][ i ] = floor( sky_mins[ 0 ][ i ] * HALF_SKY_SUBDIVISIONS ) / HALF_SKY_SUBDIVISIONS;
		sky_mins[ 1 ][ i ] = floor( sky_mins[ 1 ][ i ] * HALF_SKY_SUBDIVISIONS ) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[ 0 ][ i ] = ceil( sky_maxs[ 0 ][ i ] * HALF_SKY_SUBDIVISIONS ) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[ 1 ][ i ] = ceil( sky_maxs[ 1 ][ i ] * HALF_SKY_SUBDIVISIONS ) / HALF_SKY_SUBDIVISIONS;

		if ( ( sky_mins[ 0 ][ i ] >= sky_maxs[ 0 ][ i ] ) || ( sky_mins[ 1 ][ i ] >= sky_maxs[ 1 ][ i ] ) )
		{
			continue;
		}

		sky_mins_subd[ 0 ] = sky_mins[ 0 ][ i ] * HALF_SKY_SUBDIVISIONS;
		sky_mins_subd[ 1 ] = sky_mins[ 1 ][ i ] * HALF_SKY_SUBDIVISIONS;
		sky_maxs_subd[ 0 ] = sky_maxs[ 0 ][ i ] * HALF_SKY_SUBDIVISIONS;
		sky_maxs_subd[ 1 ] = sky_maxs[ 1 ][ i ] * HALF_SKY_SUBDIVISIONS;

		if ( sky_mins_subd[ 0 ] < -HALF_SKY_SUBDIVISIONS )
		{
			sky_mins_subd[ 0 ] = -HALF_SKY_SUBDIVISIONS;
		}
		else if ( sky_mins_subd[ 0 ] > HALF_SKY_SUBDIVISIONS )
		{
			sky_mins_subd[ 0 ] = HALF_SKY_SUBDIVISIONS;
		}

		if ( sky_mins_subd[ 1 ] < -HALF_SKY_SUBDIVISIONS )
		{
			sky_mins_subd[ 1 ] = -HALF_SKY_SUBDIVISIONS;
		}
		else if ( sky_mins_subd[ 1 ] > HALF_SKY_SUBDIVISIONS )
		{
			sky_mins_subd[ 1 ] = HALF_SKY_SUBDIVISIONS;
		}

		if ( sky_maxs_subd[ 0 ] < -HALF_SKY_SUBDIVISIONS )
		{
			sky_maxs_subd[ 0 ] = -HALF_SKY_SUBDIVISIONS;
		}
		else if ( sky_maxs_subd[ 0 ] > HALF_SKY_SUBDIVISIONS )
		{
			sky_maxs_subd[ 0 ] = HALF_SKY_SUBDIVISIONS;
		}

		if ( sky_maxs_subd[ 1 ] < -HALF_SKY_SUBDIVISIONS )
		{
			sky_maxs_subd[ 1 ] = -HALF_SKY_SUBDIVISIONS;
		}
		else if ( sky_maxs_subd[ 1 ] > HALF_SKY_SUBDIVISIONS )
		{
			sky_maxs_subd[ 1 ] = HALF_SKY_SUBDIVISIONS;
		}

		// Iterate through the subdivisions.
		for ( t = sky_mins_subd[ 1 ] + HALF_SKY_SUBDIVISIONS;
			t <= sky_maxs_subd[ 1 ] + HALF_SKY_SUBDIVISIONS;
			t++ )
		{
			for ( s = sky_mins_subd[ 0 ] + HALF_SKY_SUBDIVISIONS;
				s <= sky_maxs_subd[ 0 ] + HALF_SKY_SUBDIVISIONS;
				s++ )
			{
				MakeSkyVec( ( s - HALF_SKY_SUBDIVISIONS ) / ( float ) HALF_SKY_SUBDIVISIONS,
				            ( t - HALF_SKY_SUBDIVISIONS ) / ( float ) HALF_SKY_SUBDIVISIONS,
				            i, s_skyTexCoords[ t ][ s ], s_skyPoints[ t ][ s ] );
			}
		}

		// Only add indexes for first stage.
		FillCloudySkySide( sky_mins_subd, sky_maxs_subd, true );
	}

	Tess_UpdateVBOs( );
	GL_VertexAttribsState( tess.attribsSet );

	Tess_DrawElements();
}

static void FillCloudBox( bool addIndexes )
{
	// Iterate from 0 to 5 and not from 0 to 6,
	// we still don't want to draw the bottom, even if fullClouds.
	for ( int i = 0; i < 5; i++ )
	{
		int sky_mins_subd[ 2 ], sky_maxs_subd[ 2 ];
		const int MIN_T = -HALF_SKY_SUBDIVISIONS;

		sky_mins[ 0 ][ i ] = floor( sky_mins[ 0 ][ i ] * HALF_SKY_SUBDIVISIONS ) / HALF_SKY_SUBDIVISIONS;
		sky_mins[ 1 ][ i ] = floor( sky_mins[ 1 ][ i ] * HALF_SKY_SUBDIVISIONS ) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[ 0 ][ i ] = ceil( sky_maxs[ 0 ][ i ] * HALF_SKY_SUBDIVISIONS ) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[ 1 ][ i ] = ceil( sky_maxs[ 1 ][ i ] * HALF_SKY_SUBDIVISIONS ) / HALF_SKY_SUBDIVISIONS;

		if ( ( sky_mins[ 0 ][ i ] >= sky_maxs[ 0 ][ i ] ) || ( sky_mins[ 1 ][ i ] >= sky_maxs[ 1 ][ i ] ) )
		{
			continue;
		}

		sky_mins_subd[ 0 ] = Q_ftol( sky_mins[ 0 ][ i ] * HALF_SKY_SUBDIVISIONS );
		sky_mins_subd[ 1 ] = Q_ftol( sky_mins[ 1 ][ i ] * HALF_SKY_SUBDIVISIONS );
		sky_maxs_subd[ 0 ] = Q_ftol( sky_maxs[ 0 ][ i ] * HALF_SKY_SUBDIVISIONS );
		sky_maxs_subd[ 1 ] = Q_ftol( sky_maxs[ 1 ][ i ] * HALF_SKY_SUBDIVISIONS );

		if ( sky_mins_subd[ 0 ] < -HALF_SKY_SUBDIVISIONS )
		{
			sky_mins_subd[ 0 ] = -HALF_SKY_SUBDIVISIONS;
		}
		else if ( sky_mins_subd[ 0 ] > HALF_SKY_SUBDIVISIONS )
		{
			sky_mins_subd[ 0 ] = HALF_SKY_SUBDIVISIONS;
		}

		if ( sky_mins_subd[ 1 ] < MIN_T )
		{
			sky_mins_subd[ 1 ] = MIN_T;
		}
		else if ( sky_mins_subd[ 1 ] > HALF_SKY_SUBDIVISIONS )
		{
			sky_mins_subd[ 1 ] = HALF_SKY_SUBDIVISIONS;
		}

		if ( sky_maxs_subd[ 0 ] < -HALF_SKY_SUBDIVISIONS )
		{
			sky_maxs_subd[ 0 ] = -HALF_SKY_SUBDIVISIONS;
		}
		else if ( sky_maxs_subd[ 0 ] > HALF_SKY_SUBDIVISIONS )
		{
			sky_maxs_subd[ 0 ] = HALF_SKY_SUBDIVISIONS;
		}

		if ( sky_maxs_subd[ 1 ] < MIN_T )
		{
			sky_maxs_subd[ 1 ] = MIN_T;
		}
		else if ( sky_maxs_subd[ 1 ] > HALF_SKY_SUBDIVISIONS )
		{
			sky_maxs_subd[ 1 ] = HALF_SKY_SUBDIVISIONS;
		}

		// Iterate through the subdivisions.
		for ( int t = sky_mins_subd[ 1 ] + HALF_SKY_SUBDIVISIONS;
			t <= sky_maxs_subd[ 1 ] + HALF_SKY_SUBDIVISIONS;
			t++ )
		{
			for ( int s = sky_mins_subd[ 0 ] + HALF_SKY_SUBDIVISIONS;
				s <= sky_maxs_subd[ 0 ] + HALF_SKY_SUBDIVISIONS;
				s++ )
			{
				MakeSkyVec( ( s - HALF_SKY_SUBDIVISIONS ) / ( float ) HALF_SKY_SUBDIVISIONS,
				            ( t - HALF_SKY_SUBDIVISIONS ) / ( float ) HALF_SKY_SUBDIVISIONS, i, nullptr, s_skyPoints[ t ][ s ] );

				s_skyTexCoords[ t ][ s ][ 0 ] = s_cloudTexCoords[ i ][ t ][ s ][ 0 ];
				s_skyTexCoords[ t ][ s ][ 1 ] = s_cloudTexCoords[ i ][ t ][ s ][ 1 ];
			}
		}

		// Only add indexes for first stage.
		FillCloudySkySide( sky_mins_subd, sky_maxs_subd, addIndexes );
	}
}

static void BuildCloudData()
{
	ASSERT( tess.surfaceShader->isSky );

	// FIXME: sky_min not correct?
	sky_min = 1.0 / 256.0f;
	sky_max = 255.0 / 256.0f;

	// Set up for drawing.
	tess.multiDrawPrimitives = 0;
	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.attribsSet = 0;

	Tess_MapVBOs( false );

	shaderStage_t **lastSurfaceStage = tess.surfaceStages + MAX_SHADER_STAGES;

	if ( tess.surfaceShader->sky.cloudHeight )
	{
		shaderStage_t **surfaceStage = tess.surfaceStages;

		if ( !surfaceStage )
		{
			return;
		}

		FillCloudBox( true );

		for ( surfaceStage++;
			surfaceStage < lastSurfaceStage;
			surfaceStage++ )
		{
			if ( !surfaceStage )
			{
				break;
			}

			FillCloudBox( false );
		}
	}
}

/*
================
R_InitSkyTexCoords

Called when a sky shader is parsed.
================
*/
void R_InitSkyTexCoords( float heightCloud )
{
	// Init zfar so MakeSkyVec works even though
	// a world hasn't been bounded.
	backEnd.viewParms.zFar = 1024;

	for ( int i = 0; i < 6; i++ )
	{
		for ( int t = 0; t <= SKY_SUBDIVISIONS; t++ )
		{
			for ( int s = 0; s <= SKY_SUBDIVISIONS; s++ )
			{
				float radiusWorld = 4096;
				vec4_t skyVec;
				vec3_t v;
				float p;

				// Compute vector from view origin to sky side integral point.
				MakeSkyVec( ( s - HALF_SKY_SUBDIVISIONS ) / ( float ) HALF_SKY_SUBDIVISIONS,
				            ( t - HALF_SKY_SUBDIVISIONS ) / ( float ) HALF_SKY_SUBDIVISIONS, i, nullptr, skyVec );

				// Compute parametric value 'p' that intersects with cloud layer.
				p = ( 1.0f / ( 2 * DotProduct( skyVec, skyVec ) ) ) *
				    ( -2 * skyVec[ 2 ] * radiusWorld +
				      2 * sqrt( Square( skyVec[ 2 ] ) * Square( radiusWorld ) +
				                2 * Square( skyVec[ 0 ] ) * radiusWorld * heightCloud +
				                Square( skyVec[ 0 ] ) * Square( heightCloud ) +
				                2 * Square( skyVec[ 1 ] ) * radiusWorld * heightCloud +
				                Square( skyVec[ 1 ] ) * Square( heightCloud ) +
				                2 * Square( skyVec[ 2 ] ) * radiusWorld * heightCloud + Square( skyVec[ 2 ] ) * Square( heightCloud ) ) );

				s_cloudTexP[ i ][ t ][ s ] = p;

				// Compute intersection point based on p.
				VectorScale( skyVec, p, v );
				v[ 2 ] += radiusWorld;

				// Compute vector from world origin to intersection point 'v'.
				VectorNormalize( v );

				float sRad = acos( v[ 0 ] );
				float tRad = acos( v[ 1 ] );

				s_cloudTexCoords[ i ][ t ][ s ][ 0 ] = sRad;
				s_cloudTexCoords[ i ][ t ][ s ][ 1 ] = tRad;
			}
		}
	}
}

//======================================================================================

/*
================
Tess_StageIteratorSky

All of the visible sky triangles are in tess.

Other things could be stuck in here, like birds in the sky, etc.
================
*/
void Tess_StageIteratorSky()
{
	// Log this call.
	if ( r_logFile->integer )
	{
		// Don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va
		                  ( "--- Tess_StageIteratorSky( %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
		                    tess.numVertexes, tess.numIndexes / 3 ) );
	}

	if ( r_fastsky->integer )
	{
		return;
	}

	// trebor: HACK why does this happen with cg_draw2D 0 ?
	if ( tess.stageIteratorFunc2 == nullptr )
	{
		// tess.stageIteratorFunc2 = Tess_StageIteratorGeneric;
		Sys::Error( "tess.stageIteratorFunc == NULL" );
	}

	GL_Cull( cullType_t::CT_TWO_SIDED );

	if ( tess.stageIteratorFunc2 == &Tess_StageIteratorDepthFill )
	{
		// Go through all the polygons and project them onto
		// the sky box to see which blocks on each side need
		// to be drawn.
		Tess_ClipSkyPolygons();

		// Generate the vertexes for all the clouds, which will be drawn
		// by the generic shader routine.
		BuildCloudData();

		if ( tess.numVertexes || tess.multiDrawPrimitives )
		{
			tess.stageIteratorFunc2();
		}
	}
	else
	{
		// Go through all the polygons and project them onto
		// the sky box to see which blocks on each side need
		// to be drawn.
		Tess_ClipSkyPolygons();

		// r_showSky will let all the sky blocks be drawn in
		// front of everything to allow developers to see how
		// much sky is getting sucked in.
		if ( r_showSky->integer )
		{
			glDepthRange( 0.0, 0.0 );
		}
		else
		{
			glDepthRange( 1.0, 1.0 );
		}

		// Draw the outer skybox.
		if ( tess.surfaceShader->sky.outerbox && tess.surfaceShader->sky.outerbox != tr.blackCubeImage )
		{
			R_BindVBO( tess.vbo );
			R_BindIBO( tess.ibo );

			gl_skyboxShader->BindProgram( 0 );

			// In world space.
			gl_skyboxShader->SetUniform_ViewOrigin( backEnd.viewParms.orientation.origin );

			gl_skyboxShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
			gl_skyboxShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

			gl_skyboxShader->SetRequiredVertexPointers();

			// Bind u_ColorMap.
			GL_BindToTMU( 0, tess.surfaceShader->sky.outerbox );

			DrawSkyBox();
		}

		// Generate the vertexes for all the clouds, which will be drawn
		// by the generic shader routine.
		BuildCloudData();

		if ( tess.numVertexes || tess.multiDrawPrimitives )
		{
			tess.stageIteratorFunc2();
		}

		if ( tess.stageIteratorFunc2 != Tess_StageIteratorDepthFill )
		{
			// Back to standard depth range.
			glDepthRange( 0.0, 1.0 );

			// Note that sky was drawn so we will draw a sun later.
			backEnd.skyRenderedThisView = true;
		}
	}
}
