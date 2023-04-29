//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//
#include "basevsshader.h"
#include "convar.h"
#include "cpp_shader_constant_register_map.h"
#include "lightpass_vs30.inc"
#include "lightpass_ps30.inc"
#include "materialsystem/MaterialSystemUtil.h"
#include "lightpass_helper.h"
#include "commandbuilder.h"
#include "IDeferredExt.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar csm_enabled("r_csm", "1", 0, "0 = off, 1 = on/force");
ConVar r_csm_bias("r_csm_post_bias", "0.0");
ConVar r_csm_slopescalebias("r_csm_post_slopescale_depthbias", "1");
ConVar r_csm_performance("r_csm_performance", "0");
static ConVar mat_fullbright("mat_fullbright", "0", FCVAR_CHEAT);

static void GetTexcoordSettings(const bool bDecal, int& iNumTexcoords, int** iTexcoordDim)
{
	static int iDimDefault[] = {
		2, 0, 3,
	};

	if (bDecal)
	{
		*iTexcoordDim = iDimDefault;
		iNumTexcoords = 3;
	}
	else
	{
		*iTexcoordDim = iDimDefault;
		iNumTexcoords = 1;
	}
}
//-----------------------------------------------------------------------------
// Initialize shader parameters
//-----------------------------------------------------------------------------
void InitParamsLightPass(CBaseVSShader * pShader, IMaterialVar * *params, const char* pMaterialName, DrawLightPass_Vars_t& info)
{
	if (((info.m_nBumpmap != -1) && g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined())
		// we don't need a tangent space if we have envmap without bumpmap
		//		|| ( info.m_nEnvmap != -1 && params[info.m_nEnvmap]->IsDefined() ) 
		)
	{
		SET_FLAGS2(MATERIAL_VAR2_NEEDS_TANGENT_SPACES);
	}

	// This shader can be used with hw skinning
	SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_HW_SKINNING);
}

//-----------------------------------------------------------------------------
// Initialize shader
//-----------------------------------------------------------------------------
void InitLightPass(CBaseVSShader * pShader, IMaterialVar * *params, DrawLightPass_Vars_t& info)
{
	bool bIsBaseTextureTranslucent = false;
	if (params[info.m_nBaseTexture]->IsDefined())
	{
		pShader->LoadTexture(info.m_nBaseTexture, TEXTUREFLAGS_SRGB);

		if (params[info.m_nBaseTexture]->GetTextureValue()->IsTranslucent())
		{
			bIsBaseTextureTranslucent = true;
		}
	}

	if (params[info.m_nNoise]->IsDefined())
	{
		pShader->LoadTexture(info.m_nNoise, TEXTUREFLAGS_SRGB);
	}

	if (params[info.m_nRoughness]->IsDefined())
	{
		pShader->LoadTexture(info.m_nRoughness, TEXTUREFLAGS_SRGB);
	}

	if (params[info.m_nMetallic]->IsDefined())
	{
		pShader->LoadTexture(info.m_nMetallic, TEXTUREFLAGS_SRGB);
	}

	if (g_pConfig->UseBumpmapping())
	{
		if ((info.m_nBumpmap != -1) && params[info.m_nBumpmap]->IsDefined())
		{
			pShader->LoadBumpMap(info.m_nBumpmap);
			SET_FLAGS2(MATERIAL_VAR2_DIFFUSE_BUMPMAPPED_MODEL);
		}
	}

}

static void SetVertexShaderTextureTransform(IShaderDynamicAPI *ShaderAPI,int vertexReg, IMaterialVar *transformVar)
{
	Vector4D transformation[2];
	if (transformVar && (transformVar->GetType() == MATERIAL_VAR_TYPE_MATRIX))
	{
		const VMatrix& mat = transformVar->GetMatrixValue();
		transformation[0].Init(mat[0][0], mat[0][1], mat[0][2], mat[0][3]);
		transformation[1].Init(mat[1][0], mat[1][1], mat[1][2], mat[1][3]);
	}
	else
	{
		transformation[0].Init(1.0f, 0.0f, 0.0f, 0.0f);
		transformation[1].Init(0.0f, 1.0f, 0.0f, 0.0f);
	}
	ShaderAPI->SetVertexShaderConstant(vertexReg, transformation[0].Base(), 2);
}

//-----------------------------------------------------------------------------
// Draws the shader
//-----------------------------------------------------------------------------
void DrawLightPass(CBaseVSShader * pShader, IMaterialVar * *params, IShaderDynamicAPI * pShaderAPI, IShaderShadow * pShaderShadow,
	VertexCompressionType_t vertexCompression, CBasePerMaterialContextData * *pContextDataPtr, DrawLightPass_Vars_t & info)

{
	const bool bModel = info.bModel;	
	const bool bFastVTex = g_pHardwareConfig->HasFastVertexTextures();
	const bool bDecal = IS_FLAG_SET(MATERIAL_VAR_DECAL);

	const bool bHasBaseTexture = (info.m_nBaseTexture != -1) && params[info.m_nBaseTexture]->IsTexture();
	const bool bHasBaseTexture2 = bHasBaseTexture && (info.m_nBaseTexture2 != -1) && params[info.m_nBaseTexture2]->IsTexture();
	const bool bIsAlphaTested = IS_FLAG_SET(MATERIAL_VAR_ALPHATEST) != 0;
	const bool bHasBump = IsTextureSet(info.m_nBumpmap, params);
	const bool bHasBump2 = bHasBump && (info.m_nBumpmap2 != -1) && params[info.m_nBumpmap2]->IsTexture();
	const bool bHasMetallic = IsTextureSet(info.m_nMetallic, params);
	const bool bHasRoughness = IsTextureSet(info.m_nRoughness, params);
	//const bool bTranslucent = IS_FLAG_SET(MATERIAL_VAR_TRANSLUCENT) && bHasBaseTexture && bDecal;
	bool bUseSmoothness = info.m_nUseSmoothness != -1 && params[info.m_nUseSmoothness]->GetIntValue() == 1;

	bool bSeamlessMapping = /*((info.m_nSeamlessMappingScale != -1) &&
		(params[info.m_nSeamlessMappingScale]->GetFloatValue() != 0.0))*/ false;

	BlendType_t nBlendType = pShader->EvaluateBlendRequirements(info.m_nBaseTexture, true);
	bool bFullyOpaque = (nBlendType != BT_BLENDADD) && (nBlendType != BT_BLEND) && !bIsAlphaTested;

	if (pShader->IsSnapshotting())
	{
		pShaderShadow->EnableAlphaTest(bIsAlphaTested);

		pShaderShadow->EnableDepthWrites(false);

		if (bIsAlphaTested)
			pShaderShadow->DepthFunc(SHADER_DEPTHFUNC_EQUAL);

		pShader->EnableAlphaBlending(SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE);

		unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL;
		int userDataSize = 0;

		int* pTexCoordDim;
		int nTexCoordCount;
		GetTexcoordSettings((bModel && bFastVTex) || bDecal,
			nTexCoordCount, &pTexCoordDim);


		// Always enable...will bind white if nothing specified...
		pShaderShadow->EnableTexture(SHADER_SAMPLER1, true);		// Base (albedo) map
		pShaderShadow->EnableSRGBRead(SHADER_SAMPLER1, true);

		pShaderShadow->EnableTexture(SHADER_SAMPLER2, true);		// Base (albedo) map
		pShaderShadow->EnableSRGBRead(SHADER_SAMPLER2, true);

		pShaderShadow->EnableTexture(SHADER_SAMPLER4, true);		// Roughness
		pShaderShadow->EnableSRGBRead(SHADER_SAMPLER4, false);

		pShaderShadow->EnableTexture(SHADER_SAMPLER5, true);		// Metallic
		pShaderShadow->EnableSRGBRead(SHADER_SAMPLER5, false);

		if (bModel)
		{
			flags |= VERTEX_FORMAT_COMPRESSED;
		}

		if (bHasBump)
		{
			pShaderShadow->EnableTexture(SHADER_SAMPLER3, true);		// Bumpmap
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER1, false);
			if (bModel)
				userDataSize = 4;
			else
				flags |= VERTEX_TANGENT_SPACE;
		}

		if (bHasBump2)
		{
			pShaderShadow->EnableTexture(SHADER_SAMPLER6, true);
		}
		if (bHasBaseTexture2)
		{
			pShaderShadow->EnableTexture(SHADER_SAMPLER7, true);
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER7, true);
		}

		pShaderShadow->EnableTexture(SHADER_SAMPLER8, true);		// Shadow Depth
		pShaderShadow->SetShadowDepthFiltering(SHADER_SAMPLER8);

		if (bHasBaseTexture2 || bHasBump2)
		{
			flags |= VERTEX_COLOR;
		}

		// This shader supports compressed vertices, so OR in that flag:
		flags |= VERTEX_FORMAT_COMPRESSED;


		pShaderShadow->VertexShaderVertexFormat(flags, nTexCoordCount, pTexCoordDim, userDataSize);

		// Vertex Shader
		DECLARE_STATIC_VERTEX_SHADER(lightpass_vs30);
		SET_STATIC_VERTEX_SHADER_COMBO(MODEL, bModel);
		SET_STATIC_VERTEX_SHADER_COMBO(TANGENTSPACE, bHasBump);
		SET_STATIC_VERTEX_SHADER_COMBO(VERTEXCOLOR, IS_FLAG_SET(MATERIAL_VAR_VERTEXCOLOR));
		SET_STATIC_VERTEX_SHADER_COMBO(VERTEXALPHATEXBLENDFACTOR, bHasBaseTexture2 || bHasBump2);
		SET_STATIC_VERTEX_SHADER_COMBO(SEAMLESS, bSeamlessMapping && !bModel);
		SET_STATIC_VERTEX_SHADER(lightpass_vs30);

		// Pixel Shader
		DECLARE_STATIC_PIXEL_SHADER(lightpass_ps30);
		SET_STATIC_PIXEL_SHADER_COMBO(CONVERT_TO_SRGB, 0);
		SET_STATIC_PIXEL_SHADER_COMBO(BUMPMAP, bHasBump);
		SET_STATIC_PIXEL_SHADER_COMBO(BASETEXTURE2, bHasBaseTexture2);
		SET_STATIC_PIXEL_SHADER_COMBO(SEAMLESS, bSeamlessMapping && !bModel);
		SET_STATIC_PIXEL_SHADER_COMBO(DECAL, bDecal);
		SET_STATIC_PIXEL_SHADER_COMBO(MODEL, bModel);
		SET_STATIC_PIXEL_SHADER_COMBO(SMOOTHNESS, bUseSmoothness);
		SET_STATIC_PIXEL_SHADER(lightpass_ps30);

		pShader->DefaultFog();

		// HACK HACK HACK - enable alpha writes all the time so that we have them for underwater stuff
		pShaderShadow->EnableAlphaWrites(bFullyOpaque);
	}
	else // not snapshotting -- begin dynamic state
	{
		ITexture* pCascadedDepthTexture = (ITexture*)pShaderAPI->GetIntRenderingParameter(INT_RENDERPARM_CASCADED_DEPTHTEXTURE);
		if (!pCascadedDepthTexture)
		{
			pShader->Draw(false);
			return;
		}

		bool bLightingOnly = mat_fullbright.GetInt() == 2 && !IS_FLAG_SET(MATERIAL_VAR_NO_DEBUG_OVERRIDE);

		LightState_t lightState = { 0, false, false };
		pShaderAPI->GetDX9LightState(&lightState);

		DECLARE_DYNAMIC_VERTEX_SHADER(lightpass_vs30);
		SET_DYNAMIC_VERTEX_SHADER_COMBO(COMPRESSED_VERTS, (bModel && (int)vertexCompression) ? 1 : 0);
		SET_DYNAMIC_VERTEX_SHADER_COMBO(SKINNING, (bModel&& pShaderAPI->GetCurrentNumBones() > 0) ? 1 : 0);
		SET_DYNAMIC_VERTEX_SHADER(lightpass_vs30);

		// Set Pixel Shader Combos
		DECLARE_DYNAMIC_PIXEL_SHADER(lightpass_ps30);
		SET_DYNAMIC_PIXEL_SHADER_COMBO(PERF, MAX(0,MIN(r_csm_performance.GetInt(), 2)) ); // i just dont know anymore
		SET_DYNAMIC_PIXEL_SHADER(lightpass_ps30);


		pShader->SetModulationPixelShaderDynamicState_LinearColorSpace(1);
		pShader->SetAmbientCubeDynamicStateVertexShader();

		// Bind textures
		if (bHasBaseTexture)
		{
			if (!bLightingOnly)
			{
				pShader->BindTexture(SHADER_SAMPLER1, info.m_nBaseTexture, info.m_nBaseTextureFrame);
			}
			else
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER0, TEXTURE_GREY);
			}
		}
		else
		{
			pShaderAPI->BindStandardTexture(SHADER_SAMPLER1, TEXTURE_WHITE);
		}

		pShader->BindTexture(SHADER_SAMPLER2, info.m_nNoise);

		if (!g_pConfig->m_bFastNoBump)
		{
			if (bHasBump)
			{
				pShader->BindTexture(SHADER_SAMPLER3, info.m_nBumpmap);
			}
			else
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER3, TEXTURE_NORMALMAP_FLAT);
			}
		}
		else
		{
			pShaderAPI->BindStandardTexture(SHADER_SAMPLER3, TEXTURE_NORMALMAP_FLAT);
		}

		if (bHasRoughness)
		{
			pShader->BindTexture(SHADER_SAMPLER4, info.m_nRoughness);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SHADER_SAMPLER4, TEXTURE_WHITE);
		}

		if (bHasMetallic)
		{
			pShader->BindTexture(SHADER_SAMPLER5, info.m_nMetallic);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SHADER_SAMPLER5, TEXTURE_BLACK);
		}

		if (bSeamlessMapping && !bModel)
		{
			float SeamlessScale[1];
			SeamlessScale[0] = params[info.m_nSeamlessMappingScale]->GetFloatValue();
			pShaderAPI->SetVertexShaderConstant(
				VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, SeamlessScale);
		}
		else
		{
			if (info.m_nBaseTextureTransform != -1)
				SetVertexShaderTextureTransform(pShaderAPI ,VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, params[info.m_nBaseTextureTransform]);
		}

		// Set Pixel Shader Constants 
		float flEye[4];
		pShaderAPI->GetWorldSpaceCameraPosition(flEye);
		pShaderAPI->SetPixelShaderConstant(5, flEye);

		const lightData_Global_t csmData = GetDeferredExt()->GetLightData_Global();
		const VMatrix worldToTexture0 = *reinterpret_cast<VMatrix *>( pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_CASCADED_MATRIX_ADDRESS_0 ) );
		const Vector2D biasVar = { r_csm_slopescalebias.GetFloat(), r_csm_bias.GetFloat() };
		const Vector2D textureSize = { static_cast<float>( pCascadedDepthTexture->GetMappingHeight() * 4), static_cast<float>( pCascadedDepthTexture->GetMappingHeight() ) };

		if (pCascadedDepthTexture)
		{
			pShader->BindTexture( SHADER_SAMPLER8, pCascadedDepthTexture );
			pShaderAPI->SetPixelShaderConstant( 6, worldToTexture0.Base(), 4 );
		}

		pShaderAPI->SetPixelShaderConstant( 11, csmData.vecLight.Base() );
		pShaderAPI->SetPixelShaderConstant( 12, csmData.light.Base() );
		pShaderAPI->SetPixelShaderConstant( 13, csmData.ambient.Base() );
		pShaderAPI->SetPixelShaderConstant( 14, biasVar.Base() );
		pShaderAPI->SetPixelShaderConstant( 15, textureSize.Base() );
		pShaderAPI->SetPixelShaderConstant( 16, csmData.sizes.Base() );
	}
	pShader->Draw();
}
