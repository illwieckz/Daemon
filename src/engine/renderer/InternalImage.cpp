/*
===========================================================================

Daemon BSD Source Code
Copyright (c) 2013-2016 Daemon Developers
All rights reserved.

This file is part of the Daemon BSD Source Code (Daemon Source Code).

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Daemon developers nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL DAEMON DEVELOPERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

===========================================================================
*/
// InternalImage.cpp
#include "tr_local.h"

// Comments may include short quotes from other authors.

/*
===============
R_GetImageCustomScalingStep

Returns amount of downscaling step to be done on the given image
to perform optional picmip or other downscaling operation.

- the r_picmip cvar tells the renderer to downscale textures as many time as this cvar value;
- the r_imageMaxDimension cvar tells the renderer to downscale textures larger
  than that dimension to fit this cvar-based dimension;
- but the imageMinDimension material keyword allows an artist to tell the renderer
  to never downscale a texture under that material-based dimension,
  it has priority over r_picmip and r_ImageMaxDimension;
- while when r_imageMinDimension cvar is set to -1, textures having ImageMinDimension material
  keyword set keep their original dimension, it has priority over r_picmip;
- the imageMaxDimension material keyword allows an artist to tell the renderer
  to always downscale a texture that has a larger dimension than this material-based dimension,
- while when r_imageMaxDimension cvar is set to -1, textures having ImageMaxDimension material
  keyword set keep their original dimension, it has priority over r_picmip;

Examples of scenarii:

- r_picmip set 3 with some materials having imageMinDimension set to 128:
  attempt to reduce three times the images, but for materials having that keyword,
  stop before the image dimension would be smaller than 128;
- r_imageMaxDimension set to 64 with some materials having imageMinDimension set to 128:
  reduce images having dimension greater than 64 to fit 64 dimension, but for materials
  having that keyword, reduce image to fit 128 dimension;
- r_imageMaxDimension set to 64 with some materials having imageMinDimension set to 32:
  reduce images having dimension greater than 64 to fit 64 dimension, but for materials
  having that keyword, reduce image to fit 32 dimension;
- r_imageMaxDimension set to 64 and r_imageMinDimension set to 1024 with some materials
  having imageMinDimension set to 128:
  reduce images having dimension greater than 64 to fit 64 dimension, but for materials
  having that keyword, reduce image to fit 1024 dimension;
- r_imageMaxDimension set to 64 and r_imageMinDimension set to -1 with some materials
  having imageMinDimension set to 128:
  reduce images having dimension greater than 64 to fit 64 dimension, but for materials
  having that keyword, reduce image to fit their original dimension;
- r_picmip set to 3 and r_imageMaxDimension set to 64 with some materials having
  imageMinDimension set to 128:
  attempt to reduce three times the images, but if dimension would still be smaller than
  64, reduce to fit 64 dimension, unless the image has the material keyword set;

Examples of use cases:

- Movie producer wanting to record a movie from a demo using the highest image definition
  possible even if the game developper configured them to be downscaled for performance
  purpose:
    set r_imageMaxDimension -1
- Low budget player wanting to reduce all textures to low definition but keep configured
  minimum definition on the textures the game developer configured to not be reduced to a
  given size to keep the game playable:
    set r_imageMaxDimension 64
- Very low budger player wanting to reduce all textures to low definition in all case:
    set r_imageMaxDimension 32
    set r_imageMinDimension 32
- Competitive player wanting to reduce all textures to one flat color but keep configured
  minimum definition on the textures the game developer configured to not be reduced to a
  given size to keep the game playable:
    set r_imageMaxDimension 1
- Competitive player wanting to reduce all textures to one flat color but keep high definition
  on the textures the game developer configured to not be reduced to keep the game playable:
    ser r_imageMaxDimension 1
    set r_imageMinDimension -1
 
===============
*/
int R_GetImageCustomScalingStep( const image_t *image, const imageParams_t &imageParams )
{
	if ( image->bits & IF_NOPICMIP )
	{
		return 0;
	}

	int cvarMinDimension = std::max( -1, r_imageMinDimension->integer );
	int cvarMaxDimension = std::max( -1, r_imageMaxDimension->integer );

	if ( cvarMaxDimension == -1 )
	{
		// Keep original image dimension.
		return 0;
	}

	int imageMinDimension = std::max( 0, imageParams.minDimension );
	int imageMaxDimension = std::max( 0, imageParams.maxDimension );

	// If material has no imageMaxDimension keyword set
	// bur r_imageMaxDimension cvar is set,
	if ( imageMaxDimension == 0 && cvarMaxDimension != 0 )
	{
		// Use cvar value.
		imageMaxDimension = cvarMaxDimension;
	}

	if ( imageMinDimension != 0 )
	{
		if ( cvarMinDimension == -1 )
		{
			// Keep original image dimension.
			return 0;
		}
		else if ( cvarMinDimension != 0 )
		{
			imageMinDimension = cvarMinDimension;
		}
	}

	if ( imageMinDimension != 0 && imageMaxDimension != 0 )
	{
		// At this point, imageMaxDimension variable is reused
		// to store the image dimension we target.
		imageMaxDimension = std::max( imageMinDimension, imageMaxDimension );
	}

	int scalingStep = 0;

	// If we need to downscale the image.
	if ( imageMaxDimension != 0 )
	{
		// Downscale image to imageMaxDimension value.
		int scaledWidth = image->width;
		int scaledHeight = image->height;

		while ( imageMaxDimension < scaledWidth || imageMaxDimension < scaledHeight )
		{
			scaledWidth >>= 1;
			scaledHeight >>= 1;
			scalingStep++;
		}
	}

	int picMip = std::max( 0, r_picmip->integer );

	// If we have a minimum dimension but picMip does too much.
	if ( imageMinDimension != 0 && scalingStep < picMip )
	{
		// Do not let picMip downscale image more than imageMinDimension value.
		return scalingStep;
	}

	// Otherwise downscale the image as much as possible.
	return std::max( scalingStep, picMip );
}

void R_DownscaleImageDimensions( int scalingStep, int *scaledWidth, int *scaledHeight, const byte ***dataArray, int numLayers, int *numMips )
{
	scalingStep = std::min(scalingStep, *numMips - 1);

	if ( scalingStep > 0 )
	{
		*scaledWidth >>= scalingStep;
		*scaledHeight >>= scalingStep;

		if( *dataArray && *numMips > scalingStep ) {
			*dataArray += numLayers * scalingStep;
			*numMips -= scalingStep;
		}
	}

	// Clamp to minimum size.
	if ( *scaledWidth < 1 )
	{
		*scaledWidth = 1;
	}

	if ( *scaledHeight < 1 )
	{
		*scaledHeight = 1;
	}
}
