// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
class sdk_lightmappedgeneric_decal_vs30_Static_Index
{
public:
	sdk_lightmappedgeneric_decal_vs30_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_sdk_lightmappedgeneric_decal_vs30 1


class sdk_lightmappedgeneric_decal_vs30_Dynamic_Index
{
public:
	sdk_lightmappedgeneric_decal_vs30_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_sdk_lightmappedgeneric_decal_vs30 1
