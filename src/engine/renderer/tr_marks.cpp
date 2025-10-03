/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2008 Robert Beckebans <trebor_7@users.sourceforge.net>
Copyright (C) 2008 Oliver McFadden

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
// tr_marks.c -- polygon projection on the world polygons
#include "tr_local.h"
#include "framework/OmpSystem.h"

static const int MAX_VERTS_ON_POLY = 64;

static const int MARKER_OFFSET     = 0;

#define MARKS_ORDERED 1

using points_t = std::array<vec3_t, MAX_VERTS_ON_POLY>;
using clipPoints_t = std::array<points_t, 2>;

/*
=============
R_ChopPolyBehindPlane

Out must have space for two more vertexes than in
=============
*/
static void R_ChopPolyBehindPlane( int numInPoints, points_t& inPoints,
	int *numOutPoints, points_t& outPoints,
	vec3_t normal, vec_t dist, vec_t epsilon )
{
	// don't clip if it might overflow
	if ( numInPoints >= MAX_VERTS_ON_POLY - 2 )
	{
		*numOutPoints = 0;
		return;
	}

	int counts[ 3 ] = {};
	planeSide_t sides[ MAX_VERTS_ON_POLY + 4 ];
	float dists[ MAX_VERTS_ON_POLY + 4 ];

	// determine sides for each point
	for ( int i = 0; i < numInPoints; i++ )
	{
		float dot = DotProduct( inPoints[ i ], normal );
		dot -= dist;
		dists[ i ] = dot;

		if ( dot > epsilon )
		{
			sides[ i ] = planeSide_t::SIDE_FRONT;
		}
		else if ( dot < -epsilon )
		{
			sides[ i ] = planeSide_t::SIDE_BACK;
		}
		else
		{
			sides[ i ] = planeSide_t::SIDE_ON;
		}

		counts[ Util::ordinal(sides[ i ]) ]++;
	}

	sides[ numInPoints ] = sides[ 0 ];
	dists[ numInPoints ] = dists[ 0 ];

	*numOutPoints = 0;

	if ( !counts[ 0 ] )
	{
		return;
	}

	if ( !counts[ 1 ] )
	{
		*numOutPoints = numInPoints;
		memcpy( outPoints.data(), inPoints.data(), numInPoints * sizeof( vec3_t ) );
		return;
	}

	for ( int i = 0; i < numInPoints; i++ )
	{
		float *p1 = inPoints[ i ];
		float *clip = outPoints[ *numOutPoints ];

		if ( sides[ i ] == planeSide_t::SIDE_ON )
		{
			VectorCopy( p1, clip );
			( *numOutPoints ) ++;
			continue;
		}

		if ( sides[ i ] == planeSide_t::SIDE_FRONT )
		{
			VectorCopy( p1, clip );
			( *numOutPoints ) ++;
			clip = outPoints[ *numOutPoints ];
		}

		if ( sides[ i + 1 ] == planeSide_t::SIDE_ON || sides[ i + 1 ] == sides[ i ] )
		{
			continue;
		}

		// generate a split point
		float *p2 = inPoints[( i + 1 ) % numInPoints ];

		float d = dists[ i ] - dists[ i + 1 ];

		float dot = 0.0f;

		if ( d != 0.0f )
		{
			dot = dists[ i ] / d;
		}

		// clip xyz
		for ( int j = 0; j < 3; j++ )
		{
			clip[ j ] = p1[ j ] + dot * ( p2[ j ] - p1[ j ] );
		}

		( *numOutPoints ) ++;
	}
}

/*
=================
R_BoxSurfaces_r
=================
*/
void R_BoxSurfaces_r( bspNode_t *node, vec3_t mins, vec3_t maxs, surfaceType_t **list, int listsize, int *listlength, vec3_t dir )
{
	int          s, c;
	bspSurface_t *surf, **mark;

	// do the tail recursion in a loop
	while ( node->contents == -1 )
	{
		s = BoxOnPlaneSide( mins, maxs, node->plane );

		if ( s == 1 )
		{
			node = node->children[ 0 ];
		}
		else if ( s == 2 )
		{
			node = node->children[ 1 ];
		}
		else
		{
			R_BoxSurfaces_r( node->children[ 0 ], mins, maxs, list, listsize, listlength, dir );
			node = node->children[ 1 ];
		}
	}

	// add the individual surfaces
	mark = tr.world->markSurfaces + node->firstMarkSurface;
	c = node->numMarkSurfaces;

	while ( c-- )
	{
		//
		if ( *listlength >= listsize )
		{
			break;
		}

		//
		surf = *mark;

		// check if the surface has NOIMPACT or NOMARKS set
		if ( ( surf->shader->surfaceFlags & ( SURF_NOIMPACT | SURF_NOMARKS ) ) || ( surf->shader->contentFlags & CONTENTS_FOG ) )
		{
			surf->viewCount = tr.viewCountNoReset;
		}
		// extra check for surfaces to avoid list overflows
		else if ( * ( surf->data ) == surfaceType_t::SF_FACE )
		{
			// the face plane should go through the box
			s = BoxOnPlaneSide( mins, maxs, & ( ( srfGeneric_t* ) surf->data )->plane );

			if ( s == 1 || s == 2 )
			{
				surf->viewCount = tr.viewCountNoReset;
			}
			else if ( DotProduct( ( ( srfGeneric_t* ) surf->data )->plane.normal, dir ) > -0.5 )
			{
				// don't add faces that make sharp angles with the projection direction
				surf->viewCount = tr.viewCountNoReset;
			}
		}
		else if ( * ( surfaceType_t * )( surf->data ) != surfaceType_t::SF_GRID
		          && * ( surfaceType_t * )( surf->data ) != surfaceType_t::SF_TRIANGLES )
		{
			surf->viewCount = tr.viewCountNoReset;
		}

		// check the viewCount because the surface may have
		// already been added if it spans multiple leafs
		if ( surf->viewCount != tr.viewCountNoReset )
		{
			surf->viewCount = tr.viewCountNoReset;
			list[ *listlength ] = ( surfaceType_t * ) surf->data;
			( *listlength ) ++;
		}

		mark++;
	}
}

/*
=================
R_AddMarkFragments

=================
*/
static void R_AddMarkFragments( clipPoints_t& clipPoints,
                         int numPlanes, vec3_t *normals, float *dists,
                         int maxPoints, vec3_t pointBuffer, markFragment_t *fragmentBuffer,
                         int *returnedPoints, int *returnedFragments )
{
	int numClipPoints = 3;

	// chop the surface by all the bounding planes of the to be projected polygon
	int pingPong = 0;

	for ( int i = 0; i < numPlanes; i++ )
	{
		R_ChopPolyBehindPlane( numClipPoints, clipPoints[ pingPong ],
		                       &numClipPoints, clipPoints[ !pingPong ], normals[ i ], dists[ i ], 0.5 );
		pingPong ^= 1;

		if ( numClipPoints == 0 )
		{
			break;
		}
	}

	// completely clipped away?
	if ( numClipPoints == 0 )
	{
		return;
	}

	// add this fragment to the returned list
	if ( numClipPoints + ( *returnedPoints ) > maxPoints )
	{
		return; // not enough space for this polygon
	}

	markFragment_t *mf;
	mf = fragmentBuffer + ( *returnedFragments );
	mf->firstPoint = ( *returnedPoints );
	mf->numPoints = numClipPoints;

	memcpy( pointBuffer + ( *returnedPoints ) * 3, clipPoints[ pingPong ].data(), numClipPoints * sizeof( vec3_t ) );

	( *returnedPoints ) += numClipPoints;
	( *returnedFragments ) ++;
}

/*
=================
R_MarkFragments

=================
*/
int R_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
                     int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer )
{
	//increment view count for double check prevention
	tr.viewCountNoReset++;

	vec3_t projectionDir;
	VectorNormalize2( projection, projectionDir );
	// find all the brushes that are to be considered
	vec3_t mins, maxs;
	ClearBounds( mins, maxs );

	for ( int i = 0; i < numPoints; i++ )
	{
		vec3_t temp;

		AddPointToBounds( points[ i ], mins, maxs );
		VectorAdd( points[ i ], projection, temp );
		AddPointToBounds( temp, mins, maxs );
		// make sure we get all the leafs (also the one(s) in front of the hit surface)
		VectorMA( points[ i ], -20, projectionDir, temp );
		AddPointToBounds( temp, mins, maxs );
	}

	if ( numPoints > MAX_VERTS_ON_POLY )
	{
		numPoints = MAX_VERTS_ON_POLY;
	}

	vec3_t normals[ MAX_VERTS_ON_POLY + 2 ];
	float dists[ MAX_VERTS_ON_POLY + 2 ];

	// create the bounding planes for the to be projected polygon
	for ( int i = 0; i < numPoints; i++ )
	{
		vec3_t v1, v2;
		VectorSubtract( points[( i + 1 ) % numPoints ], points[ i ], v1 );
		VectorAdd( points[ i ], projection, v2 );
		VectorSubtract( points[ i ], v2, v2 );
		CrossProduct( v1, v2, normals[ i ] );
		VectorNormalizeFast( normals[ i ] );
		dists[ i ] = DotProduct( normals[ i ], points[ i ] );
	}

	// add near and far clipping planes for projection
	VectorCopy( projectionDir, normals[ numPoints ] );
	dists[ numPoints ] = DotProduct( normals[ numPoints ], points[ 0 ] ) - 32;
	VectorCopy( projectionDir, normals[ numPoints + 1 ] );
	VectorInverse( normals[ numPoints + 1 ] );
	dists[ numPoints + 1 ] = DotProduct( normals[ numPoints + 1 ], points[ 0 ] ) - 20;
	int numPlanes = numPoints + 2;

	int numsurfaces = 0;
	surfaceType_t *surfaces[ 64 ];
	R_BoxSurfaces_r( tr.world->nodes, mins, maxs, surfaces, 64, &numsurfaces, projectionDir );

#if MARKS_ORDERED
	std::vector<std::vector<clipPoints_t>> surfaceClipCandidates( numsurfaces );
#else
	std::vector<std::vector<clipPoints_t>> threadClipCandidates( Omp::GetThreads() );
#endif

	#pragma omp parallel
	{
#if !MARKS_ORDERED
		std::vector<clipPoints_t>& localCandidates = threadClipCandidates[ Omp::GetThreadNum() ];
#endif

		#pragma omp for schedule(dynamic, 1)
		for ( int i = 0; i < numsurfaces; i++ )
		{
#if MARKS_ORDERED
			auto& localCandidates = surfaceClipCandidates[ i ];
#endif

			if ( *surfaces[ i ] == surfaceType_t::SF_GRID )
			{
				srfGridMesh_t *cv = ( srfGridMesh_t * ) surfaces[ i ];

				for ( int m = 0; m < cv->height - 1; m++ )
				{
					for ( int n = 0; n < cv->width - 1; n++ )
					{
						// We triangulate the grid and chop all triangles within
						// the bounding planes of the to be projected polygon.
						// LOD is not taken into account, not such a big deal though.
						//
						// It's probably much nicer to chop the grid itself and deal
						// with this chopped grid as an SF_GRID surface, so LOD will
						// be applied. However the LOD of that chopped grid must
						// be synced with the LOD of the original curve.
						// One way to do this; the chopped grid shares vertices with
						// the original curve. When LOD is applied to the original
						// curve the unused vertices are flagged. Now the chopped curve
						// should skip the flagged vertices. This still leaves the
						// problems with the vertices at the chopped grid edges.
						//
						// To avoid issues when LOD applied to "hollow curves" (like
						// the ones around many jump pads) we now just add a 2 unit
						// offset to the triangle vertices.
						// The offset is added in the vertex normal vector direction
						// so all triangles will still fit together.
						// The 2 unit offset should avoid pretty much all LOD problems.

						clipPoints_t clipPoints;

						srfVert_t *dv = cv->verts + m * cv->width + n;
						vec3_t v1, v2;

						VectorCopy( dv[ 0 ].xyz, clipPoints[ 0 ][ 0 ] );
						VectorMA( clipPoints[ 0 ][ 0 ], MARKER_OFFSET, dv[ 0 ].normal, clipPoints[ 0 ][ 0 ] );
						VectorCopy( dv[ cv->width ].xyz, clipPoints[ 0 ][ 1 ] );
						VectorMA( clipPoints[ 0 ][ 1 ], MARKER_OFFSET, dv[ cv->width ].normal, clipPoints[ 0 ][ 1 ] );
						VectorCopy( dv[ 1 ].xyz, clipPoints[ 0 ][ 2 ] );
						VectorMA( clipPoints[ 0 ][ 2 ], MARKER_OFFSET, dv[ 1 ].normal, clipPoints[ 0 ][ 2 ] );
						// check the normal of this triangle
						VectorSubtract( clipPoints[ 0 ][ 0 ], clipPoints[ 0 ][ 1 ], v1 );
						VectorSubtract( clipPoints[ 0 ][ 2 ], clipPoints[ 0 ][ 1 ], v2 );
						vec3_t normal;
						CrossProduct( v1, v2, normal );
						VectorNormalizeFast( normal );

						if ( DotProduct( normal, projectionDir ) < -0.1 )
						{
							// add the fragments of this triangle
							localCandidates.push_back(clipPoints);
						}

						VectorCopy( dv[ 1 ].xyz, clipPoints[ 0 ][ 0 ] );
						VectorMA( clipPoints[ 0 ][ 0 ], MARKER_OFFSET, dv[ 1 ].normal, clipPoints[ 0 ][ 0 ] );
						VectorCopy( dv[ cv->width ].xyz, clipPoints[ 0 ][ 1 ] );
						VectorMA( clipPoints[ 0 ][ 1 ], MARKER_OFFSET, dv[ cv->width ].normal, clipPoints[ 0 ][ 1 ] );
						VectorCopy( dv[ cv->width + 1 ].xyz, clipPoints[ 0 ][ 2 ] );
						VectorMA( clipPoints[ 0 ][ 2 ], MARKER_OFFSET, dv[ cv->width + 1 ].normal, clipPoints[ 0 ][ 2 ] );
						// check the normal of this triangle
						VectorSubtract( clipPoints[ 0 ][ 0 ], clipPoints[ 0 ][ 1 ], v1 );
						VectorSubtract( clipPoints[ 0 ][ 2 ], clipPoints[ 0 ][ 1 ], v2 );
						CrossProduct( v1, v2, normal );
						VectorNormalizeFast( normal );

						if ( DotProduct( normal, projectionDir ) < -0.05 )
						{
							// add the fragments of this triangle
							localCandidates.push_back(clipPoints);
						}
					}
				}
			}
			else if ( *surfaces[ i ] == surfaceType_t::SF_FACE )
			{
				srfGeneric_t* face = ( srfGeneric_t* ) surfaces[i];

				// check the normal of this face
				if ( DotProduct( face->plane.normal, projectionDir ) > -0.5 )
				{
					continue;
				}

				srfTriangle_t *tri = face->triangles;

				for ( int k = 0; k < face->numTriangles; k++, tri++ )
				{
					clipPoints_t clipPoints;

					for ( int j = 0; j < 3; j++ )
					{
						float *v = face->verts[ tri->indexes[ j ] ].xyz;
						VectorMA( v, MARKER_OFFSET, face->plane.normal, clipPoints[ 0 ][ j ] );
					}

					// add the fragments of this face
					localCandidates.push_back(clipPoints);
				}
			}
			else if ( *surfaces[ i ] == surfaceType_t::SF_TRIANGLES && !r_noMarksOnTrisurfs->integer )
			{
				srfGeneric_t* trisurf = ( srfGeneric_t* ) surfaces[ i ];
				srfTriangle_t *tri = trisurf->triangles;

				for ( int k = 0; k < trisurf->numTriangles; k++, tri++ )
				{
					clipPoints_t clipPoints;

					for ( int j = 0; j < 3; j++ )
					{
						VectorCopy( trisurf->verts[ tri->indexes[ j ] ].xyz, clipPoints[ 0 ][ j ] );
					}

					// add the fragments of this face
					localCandidates.push_back(clipPoints);
				}
			}
		}
	}

	int returnedPoints = 0;
	int returnedFragments = 0;

#if MARKS_ORDERED
	for ( auto& surfaceCandidates : surfaceClipCandidates )
	{
		for ( auto& candidate : surfaceCandidates )
		{
#else
	for ( auto& threadCandidates : threadClipCandidates )
	{
		for ( auto& candidate : threadCandidates )
		{
#endif
			R_AddMarkFragments( candidate,
								numPlanes, normals, dists,
								maxPoints, pointBuffer,
								fragmentBuffer, &returnedPoints,
								&returnedFragments );

			if ( returnedFragments == maxFragments )
			{
				return returnedFragments;
			}
		}
	}

	return returnedFragments;
}
