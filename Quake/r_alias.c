/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

//r_alias.c -- alias model rendering

#include "quakedef.h"

extern cvar_t r_drawflat, gl_overbright_models, gl_fullbrights, r_lerpmodels, r_lerpmove; //johnfitz
extern cvar_t scr_fov, cl_gun_fovscale; // woods #zoom (ironwail)
extern cvar_t r_coloredpowerupglow; // woods
extern cvar_t r_model_light_desat; // woods - #desat
extern cvar_t r_model_light_desat_list; // woods - #desat
extern cvar_t r_outline; // woods #obmodelslist #routline
extern cvar_t r_nooutline_list; // woods #routline
extern cvar_t r_player_xray; // woods #routline

extern qboolean nameInList(const char* list, const char* name); // woods #desat #routline
extern qboolean TP_IsPlayerVisible(vec3_t origin); // woods #routline
extern qboolean IsOneVsOneMatch(void); // woods #routline
extern void R_GetEntityBounds(const entity_t *e, vec3_t mins, vec3_t maxs); // woods #routline
void Matrix3x4_RM_Transform4(const float* matrix, const float* vector, float* product); // woods #routline

cvar_t	gl_lightning_alpha = {"gl_lightning_alpha","1"}; // woods #lightalpha

const float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

extern vec3_t	lightcolor; //johnfitz -- replaces "float shadelight" for lit support

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
static const float	r_avertexnormal_dots[SHADEDOT_QUANT][256] = {
#include "anorm_dots.h"
};

extern	vec3_t			lightspot;

static const float	*shadedots = r_avertexnormal_dots[0];
static vec3_t	shadevector;

float	entalpha; //johnfitz

qboolean	overbright; //johnfitz

qboolean shading = true; //johnfitz -- if false, disable vertex shading for various reasons (fullbright, r_lightmap, showtris, etc)

//johnfitz -- struct for passing lerp information to drawing functions
typedef struct {
	short pose1;
	short pose2;
	float blend;
	vec3_t origin;
	vec3_t angles;
	bonepose_t *bonestate;
} lerpdata_t;
//johnfitz

enum
{
	ALIAS_GLSL_BASIC,
	ALIAS_GLSL_SKELETAL,
	ALIAS_GLSL_MODES
};
typedef struct
{
	int maxbones;

	GLuint program;

	// uniforms used in vert shader
	GLint bonesLoc;
	GLint blendLoc;
	GLint shadevectorLoc;
	GLint lightColorLoc;

	// uniforms used in frag shader
	GLint texLoc;
	GLint lowerTexLoc;
	GLint upperTexLoc;
	GLint fullbrightTexLoc;
	GLint useFullbrightTexLoc;
	GLint useOverbrightLoc;
	GLint useAlphaTestLoc;
	GLint colorTintLoc;
	GLint outlineWidthLoc; // woods #routline
	GLint isOutlinePassLoc; // woods #routline
	GLint outlineColorLoc; // woods #routline
	GLint shellTexLoc; // woods #powershell
	GLint useShellTexLoc; // woods #powershell
	GLint clTimeLoc; // woods #powershell
	GLint shellColorLoc; // woods #powershell
	GLint shellAlphaLoc; // woods #powershell
	GLint shellModeLoc; // woods #powershell
	GLint shellTimeLoc; // woods #powershell
	GLint shellWaveParamsLoc; // woods #powershell
	GLint fogModeLoc;
} aliasglsl_t;
static aliasglsl_t r_alias_glsl[ALIAS_GLSL_MODES];

#define pose1VertexAttrIndex 0
#define pose1NormalAttrIndex 1
#define pose2VertexAttrIndex 2
#define pose2NormalAttrIndex 3
#define texCoordsAttrIndex 4
#define vertColoursAttrIndex 5

#define boneWeightAttrIndex pose2VertexAttrIndex
#define boneIndexAttrIndex pose2NormalAttrIndex

#define ALIAS_UPPER_TEXTURE_UNIT_INDEX 3
#define ALIAS_EXTRA_SHELL_TEXTURE_UNIT_INDEX 4

/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static void *GLARB_GetXYZOffset_MDL (aliashdr_t *hdr, int pose)
{
	const size_t xyzoffs = offsetof (meshxyz_mdl_t, xyz);
	return currententity->model->meshvboptr+(hdr->vbovertofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_mdl_t)) + xyzoffs);
}
static void *GLARB_GetXYZOffset_MDLQF (aliashdr_t *hdr, int pose)
{
	const size_t xyzoffs = offsetof (meshxyz_mdl16_t, xyz);
	return currententity->model->meshvboptr+(hdr->vbovertofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_mdl16_t)) + xyzoffs);
}
static void *GLARB_GetXYZOffset_MD3 (aliashdr_t *hdr, int pose)
{
	const size_t xyzoffs = offsetof (meshxyz_md3_t, xyz);
	return currententity->model->meshvboptr+(hdr->vbovertofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_md3_t)) + xyzoffs);
}

/*
=============
GLARB_GetNormalOffset

Returns the offset of the first vertex's meshxyz_t.normal in the vbo for the
given model and pose.
=============
*/
static void *GLARB_GetNormalOffset_MDL (aliashdr_t *hdr, int pose)
{
	const size_t normaloffs = offsetof (meshxyz_mdl_t, normal);
	return currententity->model->meshvboptr+(hdr->vbovertofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_mdl_t)) + normaloffs);
}
static void *GLARB_GetNormalOffset_MDLQF (aliashdr_t *hdr, int pose)
{
	const size_t normaloffs = offsetof (meshxyz_mdl16_t, normal);
	return currententity->model->meshvboptr+(hdr->vbovertofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_mdl16_t)) + normaloffs);
}
static void *GLARB_GetNormalOffset_MD3 (aliashdr_t *hdr, int pose)
{
	const size_t normaloffs = offsetof (meshxyz_md3_t, normal);
	return currententity->model->meshvboptr+(hdr->vbovertofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_md3_t)) + normaloffs);
}

/*
=============
GLAlias_CreateShaders
=============
*/
void GLAlias_CreateShaders (void)
{
	int i;
	aliasglsl_t *glsl;
	char processedVertSource[8192], *defines;
	const glsl_attrib_binding_t bindings[] = {
		{ "TexCoords", texCoordsAttrIndex },
		{ "Pose1Vert", pose1VertexAttrIndex },
		{ "Pose1Normal", pose1NormalAttrIndex },
		{ "Pose2Vert", pose2VertexAttrIndex },
		{ "Pose2Normal", pose2NormalAttrIndex },
		{ "VertColours", vertColoursAttrIndex }
	};

	const GLchar *vertSource = \
		"#version 110\n"
		"%s"
		"\n"
		"uniform float ClTime;\n" // woods #powershell
		"varying vec2 ShellCoord;\n" // woods #powershell
		"varying vec2 ShellCoord2;\n" // woods #powershell
		"\n"
		"uniform int shellMode;\n"      // 0=normal, 1=outline, 2=shell
		"uniform float shellTime;\n"    // Time for animation
		"uniform vec4 shellWaveParams;\n" // x=amplitude, y=frequency, z=phase, w=unused
		"\n"
		"uniform vec3 ShadeVector;\n"
		"uniform vec4 LightColor;\n"
		"uniform float outlineWidth; // Amount to expand vertices\n" // woods #routline
		"uniform int isOutlinePass; // Indicates if this is the outline pass\n" // woods #routline
		"attribute vec4 TexCoords; // only xy are used \n"
		"attribute vec4 Pose1Vert;\n"
		"attribute vec3 Pose1Normal;\n"
		"#ifdef SKELETAL\n"
		"#define BoneWeight Pose2Vert\n"
		"#define BoneIndex Pose2Normal\n"
		"attribute vec4 BoneWeight;\n"
		"attribute vec4 BoneIndex;\n"
		"attribute vec4 VertColours;\n"
		"uniform vec4 BoneTable[MAXBONES*3];\n" //fixme: should probably try to use a UBO or SSBO.
		"#else\n"
		"uniform float Blend;\n"
		"attribute vec4 Pose2Vert;\n"
		"attribute vec3 Pose2Normal;\n"
		"#endif\n"
		"\n"
		"varying float FogFragCoord;\n"
		"\n"
		"float r_avertexnormal_dot(vec3 vertexnormal) // from MH \n"
		"{\n"
		"        float dot = dot(vertexnormal, ShadeVector);\n"
		"        // wtf - this reproduces anorm_dots within as reasonable a degree of tolerance as the >= 0 case\n"
		"        if (dot < 0.0)\n"
		"            return 1.0 + dot * (13.0 / 44.0);\n"
		"        else\n"
		"            return 1.0 + dot;\n"
		"}\n"
		"void main()\n"
		"{\n"
		"	gl_TexCoord[0] = TexCoords;\n"
		"\n"
		"	vec4 lerpedVert;\n" // woods #routline
		"	vec3 lerpedNormal;\n" // woods #routline
		"\n"
		"\n" // woods #powershell
		"	float s = TexCoords.x + sin(0.4 * (ClTime + TexCoords.y));\n"
		"	s *= -140.0 * (0.5 / 64.0);\n"
		"	float t = TexCoords.y + sin(0.4 * (ClTime + TexCoords.x));\n"
		"	t *= -140.0 * (0.5 / 64.0);\n"
		"	ShellCoord = vec2(s, t);\n"
		"\n"
		"	float s2 = TexCoords.x + sin(0.4 * (-ClTime + TexCoords.y));\n"
		"	s2 *= -140.0 * (0.5 / 64.0);\n"
		"	float t2 = TexCoords.y + sin(0.4 * (-ClTime + TexCoords.x));\n"
		"	t2 *= -140.0 * (0.5 / 64.0);\n"
		"	ShellCoord2 = vec2(s2, t2);\n"
		"\n"
		"#ifdef SKELETAL\n"
		"	mat4 wmat;"
		"	wmat[0]  = BoneTable[0+3*int(BoneIndex.x)] * BoneWeight.x;"
		"	wmat[0] += BoneTable[0+3*int(BoneIndex.y)] * BoneWeight.y;"
		"	wmat[0] += BoneTable[0+3*int(BoneIndex.z)] * BoneWeight.z;"
		"	wmat[0] += BoneTable[0+3*int(BoneIndex.w)] * BoneWeight.w;"
		"	wmat[1]  = BoneTable[1+3*int(BoneIndex.x)] * BoneWeight.x;"
		"	wmat[1] += BoneTable[1+3*int(BoneIndex.y)] * BoneWeight.y;"
		"	wmat[1] += BoneTable[1+3*int(BoneIndex.z)] * BoneWeight.z;"
		"	wmat[1] += BoneTable[1+3*int(BoneIndex.w)] * BoneWeight.w;"
		"	wmat[2]  = BoneTable[2+3*int(BoneIndex.x)] * BoneWeight.x;"
		"	wmat[2] += BoneTable[2+3*int(BoneIndex.y)] * BoneWeight.y;"
		"	wmat[2] += BoneTable[2+3*int(BoneIndex.z)] * BoneWeight.z;"
		"	wmat[2] += BoneTable[2+3*int(BoneIndex.w)] * BoneWeight.w;"
		"	wmat[3] = vec4(0.0,0.0,0.0,1.0);\n"
		"	float dot1 = r_avertexnormal_dot(normalize((vec4(Pose1Normal.xyz, 0.0) * wmat).xyz));\n"
		"\n"
		"	// Transform the vertex position\n" // woods #routline
		"	vec4 basePos = (vec4(Pose1Vert.xyz, 1.0) * wmat);\n"
		"	vec3 transformedNormal = normalize((vec4(Pose1Normal.xyz, 0.0) * wmat).xyz);\n"
		"   lerpedNormal = transformedNormal; // initialise for later use\n"
		"\n"
		"	float outlineScale = 1.0;\n"
		"	if (isOutlinePass == 1 && outlineWidth > 0.0)\n"
		"	{\n"
		"		// Add the scaled normal for outline\n"
			"		float weightSum = BoneWeight.x + BoneWeight.y + BoneWeight.z + BoneWeight.w;\n"
			"		if (abs(weightSum - 1.0) < 0.001)\n"
			"			lerpedVert = basePos + vec4(-transformedNormal * outlineWidth * outlineScale, 0.0);\n"
			"		else\n"
			"			lerpedVert = basePos + vec4(transformedNormal * outlineWidth * outlineScale, 0.0);\n"
		"	}\n"
		"	else\n"
		"	{\n"
		"		lerpedVert = basePos;\n"
		"	}\n"
		"\n"
		"#else\n"
		"	// Vertex position interpolation\n" // woods #routline
		"	lerpedVert = mix(vec4(Pose1Vert.xyz, 1.0), vec4(Pose2Vert.xyz, 1.0), Blend);\n"
		"	float dot1 = mix(r_avertexnormal_dot(Pose1Normal), r_avertexnormal_dot(Pose2Normal), Blend);\n"
		"\n"
		" 	// Normal interpolation\n" // woods #routline
		"	lerpedNormal = mix(Pose1Normal, Pose2Normal, Blend);\n"
		"	lerpedNormal = normalize(lerpedNormal);\n"
		"\n"
		"	// Apply outline expansion if in the outline pass\n" // woods #routline
		"	if (isOutlinePass == 1 && outlineWidth > 0.0)\n"
		"	{\n"
		"		lerpedVert.xyz += lerpedNormal * outlineWidth;\n"
		"	}\n"
		"\n"
		"#endif\n"
		"	if (shellMode == 2)\n" // woods #powershell
		"		{\n"
		"		// Start with the original vertex position\n"
		"		vec3 finalPos = lerpedVert.xyz;\n"
		"		// Get a properly normalized normal vector\n"
		"		vec3 normalDir = normalize(lerpedNormal);\n"
		"		// Add scaled offset along the normal direction using outlineWidth\n"
		"		finalPos += normalDir * outlineWidth;\n"
		"		// Add a wave effect along the normal direction\n"
		"		float wave = sin(shellTime * shellWaveParams.y + dot(lerpedVert.xyz, vec3(0.1, 0.1, 0.1)));\n"
		"		finalPos += normalDir * wave * shellWaveParams.x;\n"
		"		lerpedVert = vec4(finalPos, 1.0);\n"
		"	}\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * lerpedVert;\n"
		"	FogFragCoord = gl_Position.w;\n"
		"	gl_FrontColor = LightColor * vec4(vec3(dot1), 1.0);\n"
		"#ifdef SKELETAL\n"
		"	gl_FrontColor *= VertColours;\n"	//this is basically only useful for vertex alphas.
		"#endif\n"
		"}\n";

		const GLchar *fragSource = \
			"#version 110\n"
			"\n"
			"uniform sampler2D Tex;\n"
			"uniform sampler2D LowerTex;\n"	//team colour
			"uniform sampler2D UpperTex;\n"	//personal colour
			"uniform sampler2D FullbrightTex;\n"
			"\n"
			"uniform sampler2D ShellTex;\n" // woods #powershell
			"uniform bool UseShellTex;\n" // woods #powershell
			"uniform vec3 ShellColor;\n" // woods #powershell
			"uniform float ShellAlpha;\n" // woods #powershell
			"uniform float shellTime;\n" // woods #powershell
			"\n"
			"uniform bool UseFullbrightTex;\n"
			"uniform bool UseOverbright;\n"
			"uniform bool UseAlphaTest;\n"
			"uniform vec4 ColourTint[3];\n"	//base+bot+top+fb
			"uniform int isOutlinePass;      // Indicates if this is the outline pass\n" // woods #routline
			"uniform vec4 outlineColor;       // Color to use for the outline\n" // woods #routline
			"uniform int FogMode;\n"
			"\n"
			"varying vec2 ShellCoord;\n" // woods #powershell
			"varying vec2 ShellCoord2;\n" // woods #powershell
			"\n"
			"varying float FogFragCoord;\n"
			"\n"
			"float FogFactor(float dist)\n"
			"{\n"
			"	if (FogMode == 1)\n"
			"		return (gl_Fog.end - dist) / (gl_Fog.end - gl_Fog.start);\n"
			"	if (FogMode == 2)\n"
			"		return exp(-gl_Fog.density * dist);\n"
			"	return exp(-gl_Fog.density * gl_Fog.density * dist * dist);\n"
			"}\n"
			"\n"
			"float FogDitherHash(vec2 p)\n"
			"{\n"
			"	return fract(52.9829189 * fract(p.x * 0.06711056 + p.y * 0.00583715));\n"
			"}\n"
			"\n"
			"float FogDither()\n"
			"{\n"
			"	vec2 p = floor(gl_FragCoord.xy);\n"
			"	return (FogDitherHash(p) + FogDitherHash(p + vec2(17.0, 29.0)) - 1.0) * (1.0 / 255.0);\n"
			"}\n"
			"\n"
			"void main()\n"
			"{\n"
			"if (isOutlinePass == 1)\n"
			"    {\n"
			"        // Render the outline with a solid color\n"
			"        gl_FragColor = outlineColor;\n"
			"        return;\n"
			"    }\n"
			"    else if (isOutlinePass == 2)\n" // woods #powershell
			"	{\n"
			"        // Create a complex shell effect with animated patterns\n"
			"        float pattern = sin(gl_TexCoord[0].x * 10.0 + shellTime) * \n"
			"                       sin(gl_TexCoord[0].y * 10.0 + shellTime) * 0.25 + 0.75;\n"
			"        gl_FragColor = vec4(outlineColor.rgb * pattern, outlineColor.a);\n"
			"        return;\n"
			"    }\n"
			"\n"
			"	vec4 result = texture2D(Tex, gl_TexCoord[0].xy);\n"	//base
			"\n"
			"if (UseShellTex)\n" // woods #powershell
			"{\n"
			"    vec4 shell1 = texture2D(ShellTex, ShellCoord);\n"
			"    float brightness1 = shell1.r;\n"
			"    vec3 coloredShell1 = mix(vec3(1.0), ShellColor, brightness1);\n"
			"    shell1.rgb *= coloredShell1;\n"
			"    shell1.a = brightness1 * ShellAlpha;\n"
			"\n"
			"    vec4 shell2 = texture2D(ShellTex, ShellCoord2);\n"
			"    float brightness2 = shell2.r;\n"
			"    vec3 coloredShell2 = mix(vec3(1.0), ShellColor, brightness2);\n"
			"    shell2.rgb *= coloredShell2;\n"
			"    shell2.a = brightness2 * ShellAlpha;\n"
			"\n"
			"    vec4 combinedShell = mix(shell1, shell2, 0.5);\n"
			"    result = mix(result, combinedShell * result + result * combinedShell, combinedShell.a);\n"
			"}\n"

			"\n"
			"	if (ColourTint[0].a != 0.0) result.rgb += texture2D(LowerTex, gl_TexCoord[0].xy).rgb * ColourTint[0].rgb;\n"	//team/lower/trousers
			"	if (ColourTint[1].a != 0.0) result.rgb += texture2D(UpperTex, gl_TexCoord[0].xy).rgb * ColourTint[1].rgb;\n"	//personal/upper/torso
			"	if (UseAlphaTest && (result.a < 0.666))\n"
			"		discard;\n"
			"	result *= gl_Color;\n"	//vertex lighting results (and colormod).
			"	if (UseOverbright)\n"
			"		result.rgb *= 2.0;\n"
			"	if (UseFullbrightTex)\n"
			"		result += texture2D(FullbrightTex, gl_TexCoord[0].xy) * ColourTint[2];\n" //fullbrights (with glowmod)
			"	result = clamp(result, 0.0, 1.0);\n"
			"	float fog = FogFactor(FogFragCoord);\n"
			"	fog = clamp(fog, 0.0, 1.0) * gl_Fog.color.a;\n"
			"	result.rgb = mix(gl_Fog.color.rgb, result.rgb, fog);\n"
			"	result.rgb = clamp(result.rgb + vec3(FogDither()), 0.0, 1.0);\n"
			"	result.a *= gl_Color.a;\n" // FIXME: This will make almost transparent things cut holes though heavy fog
			"	gl_FragColor = result;\n"
			"}\n";

	for (i = 0; i < ALIAS_GLSL_MODES; i++)
		memset (&r_alias_glsl[i], 0, sizeof(r_alias_glsl[i]));

	if (!gl_glsl_alias_able)
		return;

	for (i = 0; i < ALIAS_GLSL_MODES; i++)
	{
		glsl = &r_alias_glsl[i];

		if (i == ALIAS_GLSL_SKELETAL)
		{
			defines = "#define SKELETAL\n#define MAXBONES 64\n";
			glsl->maxbones = 64;
		}
		else
		{
			defines = "";
			glsl->maxbones = 0;
		}
		q_snprintf(processedVertSource, sizeof(processedVertSource), vertSource, defines);

		glsl->program = GL_CreateProgram (processedVertSource, fragSource, sizeof(bindings)/sizeof(bindings[0]), bindings);

		if (glsl->program != 0)
		{
		// get uniform locations
			if (i == ALIAS_GLSL_SKELETAL)
			{
				glsl->bonesLoc = GL_GetUniformLocation (&glsl->program, "BoneTable");
				glsl->blendLoc = -1;
			}
			else
			{
				glsl->bonesLoc = -1;
				glsl->blendLoc = GL_GetUniformLocation (&glsl->program, "Blend");
			}
			glsl->shadevectorLoc = GL_GetUniformLocation (&glsl->program, "ShadeVector");
			glsl->lightColorLoc = GL_GetUniformLocation (&glsl->program, "LightColor");
			glsl->texLoc = GL_GetUniformLocation (&glsl->program, "Tex");
			glsl->lowerTexLoc = GL_GetUniformLocation (&glsl->program, "LowerTex");
			glsl->upperTexLoc = GL_GetUniformLocation (&glsl->program, "UpperTex");
			glsl->fullbrightTexLoc = GL_GetUniformLocation (&glsl->program, "FullbrightTex");
			glsl->useFullbrightTexLoc = GL_GetUniformLocation (&glsl->program, "UseFullbrightTex");
			glsl->useOverbrightLoc = GL_GetUniformLocation (&glsl->program, "UseOverbright");
			glsl->useAlphaTestLoc = GL_GetUniformLocation (&glsl->program, "UseAlphaTest");
			glsl->colorTintLoc = GL_GetUniformLocation (&glsl->program, "ColourTint");

			glsl->outlineWidthLoc = GL_GetUniformLocation (&glsl->program, "outlineWidth"); // woods #routline
			glsl->isOutlinePassLoc = GL_GetUniformLocation (&glsl->program, "isOutlinePass"); // woods #routline
			glsl->outlineColorLoc = GL_GetUniformLocation (&glsl->program, "outlineColor"); // woods #routline

			// woods #powershell
			glsl->clTimeLoc = GL_GetUniformLocation (&glsl->program, "ClTime");
			glsl->shellTexLoc = GL_GetUniformLocation (&glsl->program, "ShellTex");
			glsl->useShellTexLoc = GL_GetUniformLocation (&glsl->program, "UseShellTex");
			glsl->shellColorLoc = GL_GetUniformLocation (&glsl->program, "ShellColor");
			glsl->shellAlphaLoc = GL_GetUniformLocation (&glsl->program, "ShellAlpha");
			glsl->shellModeLoc = GL_GetUniformLocation(&glsl->program, "shellMode");
			glsl->shellTimeLoc = GL_GetUniformLocation(&glsl->program, "shellTime");
			glsl->shellWaveParamsLoc = GL_GetUniformLocation(&glsl->program, "shellWaveParams");
			glsl->fogModeLoc = GL_GetUniformLocation(&glsl->program, "FogMode");

			//we can do this here, its not going to change.
			GL_UseProgramFunc (glsl->program);
			GL_Uniform1iFunc (glsl->texLoc, 0);
			GL_Uniform1iFunc (glsl->fullbrightTexLoc, 1);
			GL_Uniform1iFunc (glsl->lowerTexLoc, 2);
			GL_Uniform1iFunc (glsl->upperTexLoc, ALIAS_UPPER_TEXTURE_UNIT_INDEX);
			GL_Uniform1fFunc (glsl->outlineWidthLoc, 0.0f); // woods #routline
			GL_Uniform1iFunc (glsl->isOutlinePassLoc, 0); // woods #routline
			GL_Uniform1iFunc (glsl->shellTexLoc, ALIAS_UPPER_TEXTURE_UNIT_INDEX);  // woods #powershell
			GL_Uniform1iFunc(glsl->shellModeLoc, 0);  // woods #powershell
			GL_Uniform1fFunc(glsl->shellTimeLoc, 0.0f);  // woods #powershell
			GL_Uniform4fFunc(glsl->shellWaveParamsLoc, 0.1f, 4.0f, 0.0f, 0.0f);  // woods #powershell
			GL_UseProgramFunc (0);
		}
	}
}

/*
=============
R_ParseOutlineXrayParams -- woods #routline

r_player_xray format:
	"<hexcolor> <alpha> <distance> [targets] [outline] [pcolor] [gametype=1..5] [enemycolor=0xRRGGBB] [teamcolor=0xRRGGBB]"
	"outline" draws only an xray outline around the hidden player instead
	of filling the whole hidden body.
	enemycolor/teamcolor defaults to gl_enemycolor/gl_teamcolor when omitted.
	"pcolor" uses each target player's own shirt/pants colors and overrides
	enemycolor/teamcolor + gl_enemycolor/gl_teamcolor.
	"gametype" gates xray to balanced team match sizes:
	1=1v1 only, 2=up to 2v2, ... 4=up to 4v4, 5=5v5+ (no upper cap).

Examples:
	"0xFF0000 0.8 2048"
	"2048"
	"0x00FF00 0.5"
	"0x00FF00 0.5 2048 enemy"
	"0x00FF00 0.5 2048 team"
	"0x00FF00 0.5 2048 both"
	"0xFF0000 0.8 4096 both gametype=1"
	"0xFF0000 0.8 4096 both gametype=5"
	"0xFF0000 0.8 4096 both outline"
	"pcolor 3072 both"
	"0xFF0000 0.6 3072 both enemycolor=0xFF0000 teamcolor=0x00B7FF"
	"0"
=============
*/
enum
{
	XRAY_TARGET_BOTH = 0,
	XRAY_TARGET_ENEMY,
	XRAY_TARGET_TEAM
};

enum
{
	XRAY_COLOR_SPLIT = 0,
	XRAY_COLOR_MATCH
};

enum
{
	XRAY_RENDER_FILL = 0,
	XRAY_RENDER_OUTLINE
};

static int R_ParseOutlineXrayTargetToken(const char *token)
{
	const char *value = token;
	const char *eq = strchr(token, '=');
	size_t keylen;
	if (eq && eq[1])
	{
		keylen = (size_t)(eq - token);
		if (!((keylen == 6 && !q_strncasecmp(token, "target", keylen)) ||
			(keylen == 7 && !q_strncasecmp(token, "targets", keylen))))
			return -1;
		value = eq + 1;
	}

	if (!q_strcasecmp(value, "both") ||
		!q_strcasecmp(value, "all") ||
		!q_strcasecmp(value, "players"))
		return XRAY_TARGET_BOTH;

	if (!q_strcasecmp(value, "enemy") ||
		!q_strcasecmp(value, "enemies"))
		return XRAY_TARGET_ENEMY;

	if (!q_strcasecmp(value, "team") ||
		!q_strcasecmp(value, "teammates") ||
		!q_strcasecmp(value, "ally") ||
		!q_strcasecmp(value, "allies"))
		return XRAY_TARGET_TEAM;

	return -1;
}

static int R_ParseOutlineXrayColorModeToken(const char *token)
{
	const char *value = token;
	const char *eq = strchr(token, '=');
	size_t keylen;

	if (eq && eq[1])
	{
		keylen = (size_t)(eq - token);
		if (!((keylen == 5 && !q_strncasecmp(token, "color", keylen)) ||
			(keylen == 6 && !q_strncasecmp(token, "colors", keylen)) ||
			(keylen == 9 && !q_strncasecmp(token, "colormode", keylen))))
			return -1;
		value = eq + 1;
	}

	if (!q_strcasecmp(value, "pcolor") ||
		!q_strcasecmp(value, "pcolors") ||
		!q_strcasecmp(value, "player") ||
		!q_strcasecmp(value, "playercolor") ||
		!q_strcasecmp(value, "playercolors"))
		return XRAY_COLOR_MATCH;

	return -1;
}

static int R_ParseOutlineXrayRenderModeToken(const char *token)
{
	const char *value = token;
	const char *eq = strchr(token, '=');
	size_t keylen;

	if (eq && eq[1])
	{
		keylen = (size_t)(eq - token);
		if (!((keylen == 4 && !q_strncasecmp(token, "mode", keylen)) ||
			(keylen == 5 && !q_strncasecmp(token, "style", keylen)) ||
			(keylen == 6 && !q_strncasecmp(token, "render", keylen))))
			return -1;
		value = eq + 1;
	}

	if (!q_strcasecmp(value, "outline") ||
		!q_strcasecmp(value, "outlines") ||
		!q_strcasecmp(value, "ring"))
		return XRAY_RENDER_OUTLINE;

	if (!q_strcasecmp(value, "fill") ||
		!q_strcasecmp(value, "filled") ||
		!q_strcasecmp(value, "body") ||
		!q_strcasecmp(value, "solid"))
		return XRAY_RENDER_FILL;

	return -1;
}

static int R_ParseOutlineXrayMatchSizeToken(const char *token)
{
	const char *value = token;
	const char *eq = strchr(token, '=');
	size_t keylen;
	qboolean keyed = false;
	char *endptr;
	long parsed;

	if (eq && eq[1])
	{
		keylen = (size_t)(eq - token);
		if (!(keylen == 8 && !q_strncasecmp(token, "gametype", keylen)))
			return -1;
		value = eq + 1;
		keyed = true;
	}

	if (!q_strcasecmp(value, "1v1") || !q_strcasecmp(value, "1on1"))
		return 1;
	if (!q_strcasecmp(value, "2v2") || !q_strcasecmp(value, "2on2"))
		return 2;
	if (!q_strcasecmp(value, "3v3") || !q_strcasecmp(value, "3on3"))
		return 3;
	if (!q_strcasecmp(value, "4v4") || !q_strcasecmp(value, "4on4"))
		return 4;
	if (!q_strcasecmp(value, "5v5") || !q_strcasecmp(value, "5on5"))
		return 5;

	if (!keyed)
		return -1;

	parsed = strtol(value, &endptr, 10);
	if (endptr == value || *endptr != '\0' || parsed < 1 || parsed > 5)
		return -1;

	return (int)parsed;
}

static qboolean R_ParseOutlineXrayHexColorToken(const char *token, vec3_t out_color)
{
	unsigned int rgb;

	if (q_strncasecmp(token, "0x", 2))
		return false;
	if (sscanf(token + 2, "%x", &rgb) != 1)
		return false;

	out_color[0] = ((rgb >> 16) & 0xFF) / 255.0f;
	out_color[1] = ((rgb >> 8) & 0xFF) / 255.0f;
	out_color[2] = (rgb & 0xFF) / 255.0f;
	return true;
}

static qboolean R_ParseOutlineXrayNamedColorToken(const char *token, const char *key, vec3_t out_color)
{
	const char *eq = strchr(token, '=');
	size_t keylen;

	if (!eq || !eq[1])
		return false;

	keylen = (size_t)(eq - token);
	if (strlen(key) != keylen || q_strncasecmp(token, key, keylen))
		return false;

	return R_ParseOutlineXrayHexColorToken(eq + 1, out_color);
}

static qboolean R_ParseOutlineXrayPlayerColorFallback(const char *text, vec3_t out_color)
{
	byte *rgb;
	plcolour_t parsed;

	if (!text || !text[0])
		return false;

	parsed = CL_PLColours_Parse(text);
	rgb = CL_PLColours_ToRGB(&parsed);
	if (!rgb)
		return false;

	out_color[0] = rgb[0] / 255.0f;
	out_color[1] = rgb[1] / 255.0f;
	out_color[2] = rgb[2] / 255.0f;
	return true;
}

static void R_GetPlayerMatchColor(const scoreboard_t *sb, vec3_t out_color)
{
	byte *pants_rgb;
	plcolour_t pants;

	pants = sb->pants;
	pants_rgb = CL_PLColours_ToRGB(&pants);

	/* pcolor follows player pants color directly. */
	out_color[0] = pants_rgb[0] / 255.0f;
	out_color[1] = pants_rgb[1] / 255.0f;
	out_color[2] = pants_rgb[2] / 255.0f;
}

static int R_DetectBalancedTeamMatchSize(void)
{
	int team_counts[14];
	int color;
	int active_teams = 0;
	int team_size = 0;
	int i;

	if (!cl.scores || cl.maxclients <= 0)
		return 0;

	memset(team_counts, 0, sizeof(team_counts));

	for (i = 0; i < cl.maxclients; ++i)
	{
		scoreboard_t *s = &cl.scores[i];

		if (!s->name[0] || s->spectator || s->frags == -99)
			continue;

		color = s->pants.basic;
		if (color < 1 || color > 13)
			continue;

		team_counts[color]++;
	}

	for (color = 1; color <= 13; ++color)
	{
		if (team_counts[color] <= 0)
			continue;

		++active_teams;
		if (team_size == 0)
			team_size = team_counts[color];
		else if (team_counts[color] != team_size)
			return 0;
	}

	if (active_teams != 2)
		return 0;

	return team_size;
}

static void R_ParseOutlineXrayParams(vec3_t color, vec3_t enemy_color, vec3_t team_color, float *alpha, float *dist, int *target_mode, int *color_mode, int *max_match_size, int *render_mode)
{
	const char *text = r_player_xray.string;
	qboolean saw_enemy_color = false;
	qboolean saw_team_color = false;
	qboolean saw_color = false;
	qboolean saw_alpha = false;
	qboolean saw_dist = false;
	char token[64];
	int consumed = 0;

	color[0] = 1.0f;
	color[1] = 0.0f;
	color[2] = 0.0f;
	VectorCopy(color, enemy_color);
	VectorCopy(color, team_color);
	*alpha = 1.0f;
	*dist = 0.0f;
	if (target_mode)
		*target_mode = XRAY_TARGET_BOTH;
	if (color_mode)
		*color_mode = XRAY_COLOR_SPLIT;
	if (max_match_size)
		*max_match_size = 0;
	if (render_mode)
		*render_mode = XRAY_RENDER_FILL;

	if (!text || !*text)
		return;

	while (sscanf(text, " %63s%n", token, &consumed) == 1)
	{
		int parsed_target_mode;
		int parsed_color_mode;
		int parsed_match_size;
		int parsed_render_mode;
		char *endptr;
		float value;

		text += consumed;

		if (R_ParseOutlineXrayNamedColorToken(token, "enemycolor", enemy_color))
		{
			saw_enemy_color = true;
			continue;
		}
		if (R_ParseOutlineXrayNamedColorToken(token, "teamcolor", team_color))
		{
			saw_team_color = true;
			continue;
		}

		parsed_target_mode = R_ParseOutlineXrayTargetToken(token);
		if (parsed_target_mode >= 0)
		{
			if (target_mode)
				*target_mode = parsed_target_mode;
			continue;
		}

		parsed_color_mode = R_ParseOutlineXrayColorModeToken(token);
		if (parsed_color_mode >= 0)
		{
			if (color_mode)
				*color_mode = parsed_color_mode;
			continue;
		}

		parsed_render_mode = R_ParseOutlineXrayRenderModeToken(token);
		if (parsed_render_mode >= 0)
		{
			if (render_mode)
				*render_mode = parsed_render_mode;
			continue;
		}

		parsed_match_size = R_ParseOutlineXrayMatchSizeToken(token);
		if (parsed_match_size >= 0)
		{
			if (max_match_size)
				*max_match_size = parsed_match_size;
			continue;
		}

		if (R_ParseOutlineXrayHexColorToken(token, color))
		{
			saw_color = true;
			if (!saw_enemy_color)
				VectorCopy(color, enemy_color);
			if (!saw_team_color)
				VectorCopy(color, team_color);
			continue;
		}

		value = (float)strtod(token, &endptr);
		if (endptr == token || *endptr != '\0')
			continue;

		if (!saw_alpha && value >= 0.0f && value <= 1.0f)
		{
			*alpha = value;
			saw_alpha = true;
		}
		else
		{
			*dist = q_max(0.0f, value);
			saw_dist = true;
		}
	}

	*alpha = CLAMP(0.0f, *alpha, 1.0f);

	if (!saw_dist && (saw_color || (saw_alpha && *alpha > 0.0f) || r_player_xray.value > 1.0f))
		*dist = 4096.0f;

	/* If explicit xray enemy/team colors were not provided, inherit engine
	 * enemy/team color overrides when available. */
	if (!(color_mode && *color_mode == XRAY_COLOR_MATCH))
	{
		if (!saw_enemy_color)
			R_ParseOutlineXrayPlayerColorFallback(gl_enemycolor.string, enemy_color);
		if (!saw_team_color)
			R_ParseOutlineXrayPlayerColorFallback(gl_teamcolor.string, team_color);
	}
}

/*
=============
R_IsAliasOutlineXray -- woods #routline
=============
*/
static qboolean R_IsAliasOutlineXray(entity_t *e, vec3_t color, float *alpha, float *alpha_fade, int *render_mode)
{
	char obs_buf[16], star_obs_buf[16];
	const char *obs, *star_obs;
	int local_playernum;
	int observer_viewent;
	int playernum;
	int target_mode;
	int color_mode;
	int max_match_size;
	int parsed_render_mode;
	vec3_t parsedColor;
	vec3_t parsedEnemyColor;
	vec3_t parsedTeamColor;
	float parsedAlpha;
	float dist;
	vec3_t delta;
	float distance_to_view;
	int detected_match_size;
	qboolean can_classify_team = false;
	qboolean is_observer_slot;
	qboolean is_same_team;

	if (!e->model || strcmp(e->model->name, "progs/player.mdl") || e == &cl.viewent)
		return false;

	/* Only real client player slots should get xray outlines. This excludes
	 * temp/player-model marker entities (spawn markers, dummies, etc). */
	if (!cl.entities || e <= cl.entities || e > (cl.entities + cl.maxclients))
		return false;

	playernum = (int)(e - cl.entities) - 1;
	if (playernum < 0 || playernum >= cl.maxclients)
		return false;
	if (!cl.scores || !cl.scores[playernum].name[0])
		return false;

	if (cls.demoplayback)
	{
		/* Demo userinfo observer keys are not reliable; allow xray gating
		 * without requiring observer-key checks. */
		is_observer_slot = true;
	}
	else
	{
		observer_viewent = cl.realviewentity;
		if (observer_viewent < 1 || observer_viewent > cl.maxclients)
			return false;

		obs = Info_GetKey(CL_GetSafeUserinfoForClientSlot(observer_viewent - 1), "observer", obs_buf, sizeof(obs_buf));
		star_obs = Info_GetKey(CL_GetSafeUserinfoForClientSlot(observer_viewent - 1), "*observer", star_obs_buf, sizeof(star_obs_buf));
		is_observer_slot = ((obs[0] && q_strcasecmp(obs, "off")) ||
			(star_obs[0] && q_strcasecmp(star_obs, "off")));
	}

	R_ParseOutlineXrayParams(parsedColor, parsedEnemyColor, parsedTeamColor, &parsedAlpha, &dist, &target_mode, &color_mode, &max_match_size, &parsed_render_mode);
	if (dist <= 0.0f)
		return false;

	if (max_match_size > 0 && max_match_size < 5)
	{
		detected_match_size = R_DetectBalancedTeamMatchSize();
		if (detected_match_size <= 0 || detected_match_size > max_match_size)
			return false;
	}

	if (!is_observer_slot)
		return false;

	local_playernum = -1;
	if (cl.viewentity >= 1 && cl.viewentity <= cl.maxclients)
		local_playernum = cl.viewentity - 1;

	if (local_playernum >= 0 && cl.scores[local_playernum].name[0])
	{
		can_classify_team = true;
		is_same_team = (cl.scores[playernum].pants.basic == cl.scores[local_playernum].pants.basic);
	}

	if (target_mode == XRAY_TARGET_ENEMY || target_mode == XRAY_TARGET_TEAM)
	{
		if (!can_classify_team)
			return false;

		if (target_mode == XRAY_TARGET_ENEMY && is_same_team)
			return false;
		if (target_mode == XRAY_TARGET_TEAM && !is_same_team)
			return false;
	}

	VectorSubtract(e->origin, r_origin, delta);
	distance_to_view = VectorLength(delta);
	if (distance_to_view > dist)
		return false;

	if (TP_IsPlayerVisible(e->origin))
		return false;

	if (color)
	{
		if (color_mode == XRAY_COLOR_MATCH)
			R_GetPlayerMatchColor(&cl.scores[playernum], color);
		else
		{
			if (target_mode == XRAY_TARGET_ENEMY)
				VectorCopy(parsedEnemyColor, color);
			else if (target_mode == XRAY_TARGET_TEAM)
				VectorCopy(parsedTeamColor, color);
			else if (can_classify_team)
				VectorCopy(is_same_team ? parsedTeamColor : parsedEnemyColor, color);
			else
				VectorCopy(parsedEnemyColor, color);
		}
	}
	if (alpha)
		*alpha = parsedAlpha;
	if (render_mode)
		*render_mode = parsed_render_mode;
	if (alpha_fade)
	{
		float t = CLAMP(0.0f, distance_to_view / dist, 1.0f);
		/* Smoothstep falloff: near=1.0, max range=0.0 */
		*alpha_fade = 1.0f - (t * t * (3.0f - 2.0f * t));
	}

	return true;
}

/*
=============
R_CalculateAliasModelRadius
=============
*/
static float R_CalculateAliasModelRadius(aliashdr_t *paliashdr, entity_t *e, lerpdata_t *lerpdata, qboolean *isMD5Model)
{
	float radius = 0.0f;
	maliasframedesc_t *frame = &paliashdr->frames[e->frame];

	if (isMD5Model)
		*isMD5Model = false;

	switch (paliashdr->poseverttype)
	{
	case PV_QUAKE3:  // MD3 format
	{
		int frameOffset = frame->firstpose * paliashdr->numverts;
		meshxyz_md3_t *verts = (meshxyz_md3_t *)((byte *)paliashdr + paliashdr->vertexes + (frameOffset * sizeof(meshxyz_md3_t)));
		float maxDist = 0.0f;

		for (int i = 0; i < paliashdr->numverts; i++)
		{
			float x = (float)verts[i].xyz[0] * (1.0f / 64.0f) * paliashdr->scale[0];
			float y = (float)verts[i].xyz[1] * (1.0f / 64.0f) * paliashdr->scale[1];
			float z = (float)verts[i].xyz[2] * (1.0f / 64.0f) * paliashdr->scale[2];
			float dist = sqrt(x * x + y * y + z * z);

			if (dist > maxDist)
				maxDist = dist;
		}

		radius = maxDist;
		break;
	}

	case PV_IQM:
	{
		const iqmvert_t *verts = (const iqmvert_t *)((byte *)paliashdr + paliashdr->vertexes);
		float maxDist = 0.0f;
		qboolean isMD5 = true;

		for (int i = 0; i < paliashdr->numverts && isMD5; i++)
		{
			float weightSum = verts[i].weight[0] + verts[i].weight[1] + verts[i].weight[2] + verts[i].weight[3];
			if (weightSum < 0.999f || weightSum > 1.001f)
				isMD5 = false;
		}

		if (lerpdata && lerpdata->bonestate)
		{
			for (int i = 0; i < paliashdr->numverts; i++)
			{
				vec3_t transformedVert = {0, 0, 0};

				for (int j = 0; j < 4; j++)
				{
					if (verts[i].weight[j] > 0.0f)
					{
						vec3_t pos;
						Matrix3x4_RM_Transform4(lerpdata->bonestate[verts[i].idx[j]].mat, verts[i].xyz, pos);
						VectorMA(transformedVert, verts[i].weight[j], pos, transformedVert);
					}
				}

				float scaledVert[3];
				scaledVert[0] = transformedVert[0] * paliashdr->scale[0];
				scaledVert[1] = transformedVert[1] * paliashdr->scale[1];
				scaledVert[2] = transformedVert[2] * paliashdr->scale[2];

				float dist = sqrt(scaledVert[0] * scaledVert[0] +
					scaledVert[1] * scaledVert[1] +
					scaledVert[2] * scaledVert[2]);
				maxDist = q_max(maxDist, dist);
			}
		}
		else
		{
			for (int i = 0; i < paliashdr->numverts; i++)
			{
				float scaledVert[3];
				scaledVert[0] = verts[i].xyz[0] * paliashdr->scale[0];
				scaledVert[1] = verts[i].xyz[1] * paliashdr->scale[1];
				scaledVert[2] = verts[i].xyz[2] * paliashdr->scale[2];

				float dist = sqrt(scaledVert[0] * scaledVert[0] +
					scaledVert[1] * scaledVert[1] +
					scaledVert[2] * scaledVert[2]);
				maxDist = q_max(maxDist, dist);
			}
		}

		if (isMD5Model)
			*isMD5Model = isMD5;
		radius = maxDist;
		break;
	}

	case PV_QUAKE1:  // Standard MDL format
	default:
	{
		if (paliashdr->boundingradius > 0)
		{
			radius = paliashdr->boundingradius;
		}
		else
		{
			trivertx_t *verts = (trivertx_t *)((byte *)paliashdr + paliashdr->vertexes);
			float maxDist = 0.0f;

			verts += frame->firstpose * paliashdr->numverts;

			for (int i = 0; i < paliashdr->numverts; i++)
			{
				float dx = verts[i].v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
				float dy = verts[i].v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
				float dz = verts[i].v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];
				float dist = sqrt(dx * dx + dy * dy + dz * dz);
				maxDist = q_max(maxDist, dist);
			}

			radius = maxDist;
		}
		break;
	}
	}

	return q_max(radius, 1.0f);
}

/*
=============
R_CalculateAliasModelOutlineWidth -- woods #routline
=============
*/
float R_CalculateAliasModelOutlineWidth(aliashdr_t* paliashdr, entity_t* e, lerpdata_t* lerpdata, qboolean is_xray, qboolean is_xray_outline)
{
	float outlineWidth;

	if (!is_xray && (r_outline.value <= 0 ||
		cl.viewent.model == e->model ||
		(e->model->flags & MOD_NOOUTLINE)))
		return 0.0f;

	qboolean isMD5Model = false;
	float radius = R_CalculateAliasModelRadius(paliashdr, e, lerpdata, &isMD5Model);
	float modelScale = 50.0f / radius;
	float finalScale = modelScale / 1.5;
	const float md5OutlineScale = 0.25f;
	if (isMD5Model)
		finalScale *= md5OutlineScale; // MD5 models render larger in shell/outline space, so keep them in line with classic models
	if (is_xray && modelScale < 1.0f && (!is_xray_outline || isMD5Model))
		return 1.0f;

	float cvarValue;
	if (is_xray && r_outline.value <= 0.0f)
		cvarValue = 1.0f;
	else
		cvarValue = CLAMP(1.0f, r_outline.value, 5.0f);

	outlineWidth = cvarValue * finalScale;

	// Filled xray and MD5 outline keep the old fixed minimum above. Classic
	// outline-only xray should look more like the normal r_outline shell; when
	// r_outline is off, scale from the MD5 fallback using the same proportion
	// as the normal outline path.
	if (is_xray_outline && r_outline.value <= 0.0f && !isMD5Model)
		outlineWidth = q_max(outlineWidth, 1.0f / md5OutlineScale);

	return outlineWidth;
}

/*
=============
R_BeginAliasOutlineRendering -- woods #routline
=============
*/
void R_BeginAliasOutlineRendering(aliasglsl_t* glsl)
{
	GLuint stencil_ref = 1;
	GLuint stencil_mask = 1;
	GLuint viewmodel_stencil_bit = GL_VIEWMODEL_STENCIL_BIT();

	// Save the current OpenGL state that we are going to modify
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_POLYGON_BIT);

	glEnable(GL_STENCIL_TEST);

	// For viewmodels, also write the viewmodel bit so LaserSight can mask it
	if (viewmodel_stencil_bit && gl_laserpoint.value && currententity && currententity == &cl.viewent)
	{
		stencil_ref |= viewmodel_stencil_bit;
		stencil_mask |= viewmodel_stencil_bit;
	}

	// Configure stencil to write on the stencil buffer where the model is drawn
	glStencilFunc(GL_ALWAYS, (GLint)stencil_ref, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glStencilMask(stencil_mask);

	// Enable depth testing and write to depth buffer
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	// Set uniforms for the main model pass (no outline)
	GL_Uniform1fFunc(glsl->outlineWidthLoc, 0.0f); // No outline expansion
	GL_Uniform1iFunc(glsl->isOutlinePassLoc, 0);
}

/*
=============
R_IsOutlineBoundsFadeEnabled -- woods #routline

Enable outline bounds fade only when in noclip-like modes.
Includes CRMod7 observer fly (userinfo "observer" = "fly").
=============
*/
static qboolean R_IsOutlineBoundsFadeEnabled(void)
{
	if (noclip_anglehack)
		return true;

	if (sv.active && sv_player &&
		(sv_player->v.movetype == MOVETYPE_NOCLIP || sv_player->v.movetype == MOVETYPE_FLY))
		return true;

	if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected) &&
		cl.realviewentity >= 1 && cl.realviewentity <= cl.maxclients)
	{
		const char *userinfo = CL_GetSafeRealViewEntityUserinfo();
		char buf[16], buf2[16];
		const char* obs = Info_GetKey(userinfo, "observer", buf, sizeof(buf));
		const char* star_obs = Info_GetKey(userinfo, "*observer", buf2, sizeof(buf2));
		if (!strcmp(obs, "fly") || !strcmp(star_obs, "fly"))
			return true;
	}

	return false;
}

/*
=============
R_GetEntityBoundsFadeFactor -- woods #routline

Returns a fade factor (0.0 to 1.0) based on view proximity to entity bounds.
- Outside expanded bounds: 1.0 (full opacity)
- Entering expanded bounds: fades from 1.0 to 0.0
- Reaches 0.0 at 50% of the way from expanded boundary to center
=============
*/
static float R_GetEntityBoundsFadeFactor(const entity_t *e)
{
	vec3_t mins, maxs;
	int i, nearest_axis;
	float min_dist, dist_to_min, dist_to_max, nearest;
	float half_size, fade_distance;
	float expand = 24.0f; // Expand bounds outward so fade starts earlier

	R_GetEntityBounds(e, mins, maxs);

	// Expand bounds outward
	for (i = 0; i < 3; i++)
	{
		mins[i] -= expand;
		maxs[i] += expand;
	}

	// Check if view is outside expanded bounds on any axis
	for (i = 0; i < 3; i++)
	{
		if (r_refdef.vieworg[i] < mins[i] || r_refdef.vieworg[i] > maxs[i])
			return 1.0f; // Outside bounds, full opacity
	}

	// View is inside expanded bounds - find nearest face and its axis
	min_dist = 999999.0f;
	nearest_axis = 0;
	for (i = 0; i < 3; i++)
	{
		dist_to_min = r_refdef.vieworg[i] - mins[i];
		dist_to_max = maxs[i] - r_refdef.vieworg[i];
		nearest = q_min(dist_to_min, dist_to_max);
		if (nearest < min_dist)
		{
			min_dist = nearest;
			nearest_axis = i;
		}
	}

	// Calculate fade distance as 50% of the way to center on that axis
	half_size = (maxs[nearest_axis] - mins[nearest_axis]) * 0.5f;
	fade_distance = half_size * 0.5f; // Fade completes at 50% to center

	if (fade_distance <= 0.0f)
		return 0.0f;

	// min_dist is how far inside we are from the nearest face
	// At boundary (min_dist ~= 0): alpha = 1.0
	// At fade_distance inside (50% to center): alpha = 0.0
	return CLAMP(0.0f, 1.0f - (min_dist / fade_distance), 1.0f);
}

/*
=============
R_DrawAliasModelOutline -- woods #routline
=============
*/
void R_DrawAliasModelOutline(aliasglsl_t* glsl, aliashdr_t* paliashdr, lerpdata_t* lerpdata, entity_t* e)
{
	vec3_t xrayColor = { 1.0f, 0.0f, 0.0f };
	float xrayAlpha = 1.0f;
	float xrayAlphaFade = 1.0f;
	int xrayRenderMode = XRAY_RENDER_FILL;
	qboolean is_xray = R_IsAliasOutlineXray(e, xrayColor, &xrayAlpha, &xrayAlphaFade, &xrayRenderMode);
	GLuint outline_stencil_mask = (gl_laserpoint.value && GL_VIEWMODEL_STENCIL_BIT()) ? 0x01u : 0xFFu;

	if (!is_xray && !(r_outline.value > 0 &&
		!(cl.viewent.model == e->model) &&
		!(e->model->flags & MOD_NOOUTLINE)))
		return;

	if (!strcmp(e->model->name, "progs/eyes.mdl"))
	{
		qboolean allow_outline = false;

		if (cls.demoplayback) 
			allow_outline = true;
		else if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
		{
			const char *userinfo = CL_GetSafeRealViewEntityUserinfo();
			char buf[16], buf2[16];
			const char* obs, * star_obs;

			obs = Info_GetKey(userinfo, "observer", buf, sizeof(buf));
			star_obs = Info_GetKey(userinfo, "*observer", buf2, sizeof(buf2));

			if (cl.modtype == 1 || cl.modtype == 4) // mods with observer keys
			{
				if ((strcmp(obs, "eyecam") == 0 || strcmp(star_obs, "eyecam") == 0) ||
					(strcmp(obs, "chase") == 0 || strcmp(star_obs, "chase") == 0) ||
					(strcmp(obs, "fly") == 0 || strcmp(star_obs, "fly") == 0) ||
					(strcmp(obs, "walk") == 0 || strcmp(star_obs, "walk") == 0))
				{
					allow_outline = true;
				}
			}
			else if (strcmp(cl.observer, "n") != 0) // general observer flag for legacy mods/servers
				allow_outline = true;
		}

		if (!allow_outline)
			return;
	}

	float outlineWidth = R_CalculateAliasModelOutlineWidth(paliashdr, e, lerpdata, is_xray,
		is_xray && xrayRenderMode == XRAY_RENDER_OUTLINE);

	if (outlineWidth <= 0.0f)
		return;

	// Fade out outline as view enters entity bounds (noclip/fly only)
	float boundsFade = 1.0f;
	if (R_IsOutlineBoundsFadeEnabled())
	{
		boundsFade = R_GetEntityBoundsFadeFactor(e);
		if (boundsFade <= 0.0f)
			return;
	}

	// Pick a dedicated xray stencil bit that is neither the alias mask bit
	// (0x01) nor the viewmodel/laser bit. We use it as a coverage mask so the
	// xray silhouette blends one flat translucent color per pixel instead of
	// accumulating (darkening) where model geometry overlaps along the view ray.
	// If no such bit is free, fall back to the legacy single-pass blend.
	GLuint xray_bit = 0;
	if (is_xray && gl_stencilbits)
	{
		GLuint vm_bit = GL_VIEWMODEL_STENCIL_BIT();
		int bitnum;
		for (bitnum = 1; bitnum < gl_stencilbits && bitnum < 32; ++bitnum)
		{
			GLuint b = 1u << bitnum;
			if (b != vm_bit) // b is never 0x01: loop starts at 0x02
			{
				xray_bit = b;
				break;
			}
		}
	}

	if (is_xray)
	{
		// Xray draws the part of the model behind world geometry; GL_GREATER
		// (below) selects those occluded fragments. With a dedicated bit we use
		// the stencil as a coverage mask; otherwise skip the stencil test so
		// stale values from earlier entities can't block the silhouette.
		if (xray_bit)
		{
			glEnable(GL_STENCIL_TEST);
			glStencilMask(xray_bit);
		}
		else
			glDisable(GL_STENCIL_TEST);
	}
	else
	{
		// Configure stencil to only draw where stencil is not set by the model
		glStencilFunc(GL_NOTEQUAL, 1, (GLint)outline_stencil_mask); // Pass test where stencil is not set by the model
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); // Keep the stencil buffer unchanged
		glStencilMask(0x00); // Disable writing to the stencil buffer
	}

	// Disable depth writing to prevent depth buffer modifications
	glDepthMask(GL_FALSE);
	if (is_xray)
		glDepthFunc(GL_GREATER);
	else
		glDepthFunc(GL_LEQUAL);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Expand the vertices along normals for the outline
	GL_Uniform1iFunc(glsl->isOutlinePassLoc, 1);
	GL_Uniform1fFunc(glsl->outlineWidthLoc, outlineWidth);
	float outlineColor[4] = { 0.0f, 0.0f, 0.0f, entalpha }; // Outline color

	if (is_xray)
	{
		outlineColor[0] = xrayColor[0];
		outlineColor[1] = xrayColor[1];
		outlineColor[2] = xrayColor[2];
		outlineColor[3] = xrayAlpha * xrayAlphaFade;
	}
	else if (!strcmp(e->model->name, "progs/flag.mdl") && e->skinnum == 0)
	{
		outlineColor[0] = 1.0f; // Change red component
		outlineColor[3] = 0.2f; // Change alpha component
	}
	else if (!strcmp(e->model->name, "progs/flag.mdl") && e->skinnum == 1)
	{
		outlineColor[2] = 1.0f; // Change blue component
		outlineColor[3] = 0.2f; // Change alpha component
	}
	else if (!strcmp(e->model->name, "progs/quaddama.mdl"))
	{
		outlineColor[2] = 1.0f; // Change blue component
		outlineColor[3] = 0.2f; // Change alpha component
	}
	else if (!strcmp(e->model->name, "progs/invulner.mdl"))
	{
		outlineColor[0] = 1.0f; // Change red component
		outlineColor[3] = 0.2f; // Change alpha component
	}
	else if (!strcmp(e->model->name, "progs/invisibl.mdl"))
	{
		outlineColor[0] = 191.0f / 255.0f; // Change red component
		outlineColor[1] = 160.0f / 255.0f; // Change green component
		outlineColor[2] = 2.0f / 255.0f; // Change blue component
		outlineColor[3] = 0.2f; // Change alpha component
	}

	// Apply bounds proximity fade
	outlineColor[3] *= boundsFade;

	GL_Uniform4fvFunc(glsl->outlineColorLoc, 1, outlineColor);

	if (is_xray && xray_bit && xrayRenderMode == XRAY_RENDER_OUTLINE)
	{
		// Xray-outline mode mirrors normal r_outline behavior: mark the
		// expanded hidden shell, subtract the hidden body, then fill the
		// remaining ring once per pixel.
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

		GL_Uniform1fFunc(glsl->outlineWidthLoc, outlineWidth);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		glStencilFunc(GL_ALWAYS, (GLint)xray_bit, (GLint)xray_bit);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
			e->model->meshindexesvboptr + paliashdr->eboofs);

		// Clear the original body out of the shell mask. The fill pass below
		// redraws the same expanded shell and consumes the remaining mask.
		GL_Uniform1fFunc(glsl->outlineWidthLoc, 0.0f);
		glDisable(GL_CULL_FACE);
		glStencilFunc(GL_EQUAL, (GLint)xray_bit, (GLint)xray_bit);
		glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
		glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
			e->model->meshindexesvboptr + paliashdr->eboofs);

		GL_Uniform1fFunc(glsl->outlineWidthLoc, outlineWidth);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		glStencilFunc(GL_EQUAL, (GLint)xray_bit, (GLint)xray_bit);
		glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
		glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
			e->model->meshindexesvboptr + paliashdr->eboofs);
	}
	else
	{
		// Cull front faces to render back-facing triangles
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);

		if (is_xray && xray_bit)
		{
			// Coverage-mask fill (two passes, identical geometry/depth state so the
			// same fragments rasterize in both). Pass 1: mark every occluded pixel
			// in the xray bit with color writes disabled.
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
			glStencilFunc(GL_ALWAYS, (GLint)xray_bit, (GLint)xray_bit);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
				e->model->meshindexesvboptr + paliashdr->eboofs);

			// Pass 2: blend the color exactly once per marked pixel. The first
			// depth-passing fragment clears the xray bit (GL_ZERO), so any later
			// overlapping fragment for that pixel fails the stencil test and does
			// not darken it again. Every marked pixel had a passing fragment in
			// pass 1, so the identical pass 2 clears the bit; no residue.
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glStencilFunc(GL_EQUAL, (GLint)xray_bit, (GLint)xray_bit);
			glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
			glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
				e->model->meshindexesvboptr + paliashdr->eboofs);
		}
		else
		{
			// Legacy single-pass outline / xray fallback.
			glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
				e->model->meshindexesvboptr + paliashdr->eboofs);
		}
	}

	// Reset face culling
	glCullFace(GL_BACK);
	glDisable(GL_CULL_FACE);

	// Reset depth mask and stencil mask
	glDepthMask(GL_TRUE);
	glStencilMask(0xFF);
	glDepthRange(0, 1);

	glDisable(GL_BLEND);
}

/*
=============
R_DrawViewmodelShell -- woods #powershell
=============
*/
void R_DrawViewmodelShell(aliasglsl_t* glsl, aliashdr_t* paliashdr, lerpdata_t* lerpdata, entity_t* e)
{
	GLuint viewmodel_stencil_bit = GL_VIEWMODEL_STENCIL_BIT();
	int powerup_items = cl.items | (M_LivePreview_UsePowerupShells() ? IT_QUAD : 0);

	if (!r_coloredpowerupglow.value
		|| gl_powerupshells.value <= 0.0f
		|| !gl_stencilbits // woods #powershell -- no stencil buffer means no rim mask; skip rather than flood the whole model
		|| e != &cl.viewent
		|| !(powerup_items & (IT_QUAD | IT_INVULNERABILITY))
		|| chase_active.value)
	{
		return;
	}

	qboolean isMD5Model = false;
	float modelRadius = R_CalculateAliasModelRadius(paliashdr, e, lerpdata, &isMD5Model);

	float baseScale = 14.0f;
	float inverseScale = baseScale * pow(15.0f / modelRadius, 1.6f);
	float shellScale = CLAMP(0.08f, inverseScale, 8.0f);
	if (isMD5Model)
		shellScale *= 1.30f;

	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glEnable(GL_STENCIL_TEST);
	if (viewmodel_stencil_bit && gl_laserpoint.value)
	{
		// Keep the existing outline bit intact while stamping the viewmodel bit
		// onto the expanded shell so LaserSight stays behind the full shell.
		glStencilFunc(GL_NOTEQUAL, (GLint)(1u | viewmodel_stencil_bit), 0x01);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glStencilMask(viewmodel_stencil_bit);
	}
	else
	{
		glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		glStencilMask(0x00);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	// Disable depth writing but enable depth test
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LEQUAL);

	float waveAmp = 0.0f;      // Set to 0 to disable wave animation
	float waveFreq = 0.0f;     // Not used when waveAmp is 0

	const float kmax = 5.0f;
	float alpha_knob = CLAMP(0.0f, gl_powerupshells_alpha.value, 1.0f);
	float k = alpha_knob * kmax;
	float shellAlpha = CLAMP(0.0f, 0.1f * k, 1.0f);

	float shellColor[4] = { 0.0f, 0.0f, 0.0f, shellAlpha };

	if ((cl.time <= cl.faceanimtime || M_LivePreview_UseDamageTint()) && cl_damagehue.value)
	{
		plcolour_t dhvalue = CL_PLColours_Parse(cl_damagehuecolor.string);
		byte* dhuecolor = CL_PLColours_ToRGB(&dhvalue);
		
		shellColor[0] = dhuecolor[0] / 255.0f;
		shellColor[1] = dhuecolor[1] / 255.0f;
		shellColor[2] = dhuecolor[2] / 255.0f;
	}
	else
	{
	if ((powerup_items & IT_QUAD) && (powerup_items & IT_INVULNERABILITY))
	{
		shellColor[0] = 1.0f;  // Red
		shellColor[2] = 1.0f;  // Blue
	}
	else if (powerup_items & IT_QUAD)
		shellColor[2] = 1.0f;  // Blue
	else if (powerup_items & IT_INVULNERABILITY)
		shellColor[0] = 1.0f;  // Red
	}

	GL_Uniform1iFunc(glsl->shellModeLoc, 2);
	GL_Uniform1iFunc(glsl->isOutlinePassLoc, 2);
	GL_Uniform1fFunc(glsl->outlineWidthLoc, shellScale);
	GL_Uniform4fFunc(glsl->outlineColorLoc, shellColor[0], shellColor[1], shellColor[2], shellAlpha);
	GL_Uniform1fFunc(glsl->shellTimeLoc, 0); // Set to 0 to disable time-based effects
	GL_Uniform4fFunc(glsl->shellWaveParamsLoc, waveAmp, waveFreq, 0.0f, 0.0f);

	// Draw the shell
	glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
		e->model->meshindexesvboptr + paliashdr->eboofs);

	glPopAttrib();

	// Reset shader uniforms
	GL_Uniform1iFunc(glsl->isOutlinePassLoc, 0);
	GL_Uniform1iFunc(glsl->shellModeLoc, 0);
}

static qboolean R_AliasShellTextureUnit(struct skintextures_s tex, int *unit)
{
	if (tex.upper)
	{
		if (gl_max_texture_image_units <= ALIAS_EXTRA_SHELL_TEXTURE_UNIT_INDEX)
			return false;
		*unit = ALIAS_EXTRA_SHELL_TEXTURE_UNIT_INDEX;
		return true;
	}

	if (gl_max_texture_image_units <= ALIAS_UPPER_TEXTURE_UNIT_INDEX)
		return false;
	*unit = ALIAS_UPPER_TEXTURE_UNIT_INDEX;
	return true;
}

static void ApplyShellEffect(aliasglsl_t* glsl, float red, float green, float blue, float time, float alpha, int shell_unit) // -- woods #powershell
{
	GL_Uniform1iFunc(glsl->useShellTexLoc, 1);
	GL_Uniform1iFunc(glsl->shellTexLoc, shell_unit);
	GL_SelectTexture(GL_TEXTURE0_ARB + shell_unit);
	GL_ClearBindings();
	GL_Bind(shelltexture);
	GL_Uniform1fFunc(glsl->clTimeLoc, time);
	GL_Uniform3fFunc(glsl->shellColorLoc, red, green, blue);
	GL_Uniform1fFunc(glsl->shellAlphaLoc, alpha);
	GL_SelectTexture(GL_TEXTURE0);
}

static qboolean R_GetPowerupPickupShellColor(entity_t* e, vec3_t color) // -- woods #powershell
{
	if (gl_powerupshells.value < 2.0f || !e || !e->model)
		return false;

	if (!strcmp(e->model->name, "progs/quaddama.mdl"))
	{
		color[0] = 0.0f;
		color[1] = 0.0f;
		color[2] = 1.0f;
		return true;
	}

	if (!strcmp(e->model->name, "progs/invulner.mdl"))
	{
		color[0] = 1.0f;
		color[1] = 0.0f;
		color[2] = 0.0f;
		return true;
	}

	return false;
}

static float R_PowerupShellTextureAlpha(void) // -- woods #powershell
{
	float base = CLAMP(0.0f, gl_powerupshells.value, 1.0f);
	const float kmax = 5.0f;
	float alpha_knob = CLAMP(0.0f, gl_powerupshells_alpha.value, 1.0f);
	float k = alpha_knob * kmax;

	return 1.0f - powf(1.0f - base, k);
}

static void R_DrawPowerupPickupShell(aliasglsl_t* glsl, aliashdr_t* paliashdr, lerpdata_t* lerpdata, entity_t* e) // -- woods #powershell
{
	vec3_t shellColor;
	qboolean isMD5Model = false;
	float modelRadius, inverseScale, shellScale, shellAlpha;
	const float kmax = 5.0f;
	float alpha_knob, k;

	if (!r_coloredpowerupglow.value || !gl_stencilbits || !R_GetPowerupPickupShellColor(e, shellColor))
		return; // woods #powershell -- no stencil buffer means no rim mask; skip rather than flood the whole model

	modelRadius = R_CalculateAliasModelRadius(paliashdr, e, lerpdata, &isMD5Model);
	inverseScale = 14.0f * pow(15.0f / modelRadius, 1.6f);
	shellScale = CLAMP(0.08f, inverseScale, 8.0f);
	if (isMD5Model)
		shellScale *= 1.30f;

	alpha_knob = CLAMP(0.0f, gl_powerupshells_alpha.value, 1.0f);
	k = alpha_knob * kmax;
	shellAlpha = CLAMP(0.0f, 0.1f * k, 1.0f);
	if (shellAlpha <= 0.0001f)
		return;

	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilMask(0x00);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LEQUAL);

	GL_Uniform1iFunc(glsl->shellModeLoc, 2);
	GL_Uniform1iFunc(glsl->isOutlinePassLoc, 2);
	GL_Uniform1fFunc(glsl->outlineWidthLoc, shellScale);
	GL_Uniform4fFunc(glsl->outlineColorLoc, shellColor[0], shellColor[1], shellColor[2], shellAlpha);
	GL_Uniform1fFunc(glsl->shellTimeLoc, 0);
	GL_Uniform4fFunc(glsl->shellWaveParamsLoc, 0.0f, 0.0f, 0.0f, 0.0f);

	glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
		e->model->meshindexesvboptr + paliashdr->eboofs);

	glPopAttrib();

	GL_Uniform1iFunc(glsl->isOutlinePassLoc, 0);
	GL_Uniform1iFunc(glsl->shellModeLoc, 0);
}

static qboolean R_ApplyPowerupShellEffect(aliasglsl_t* glsl, entity_t* e, struct skintextures_s tex, int *shell_unit) // -- woods #powershell
{
	int powerup_items = cl.items | (M_LivePreview_UsePowerupShells() ? IT_QUAD : 0);
	vec3_t pickup_shell_color;

	*shell_unit = -1;
	GL_Uniform1iFunc(glsl->useShellTexLoc, 0);

	if (!r_coloredpowerupglow.value || gl_powerupshells.value <= 0.0f)
		return false;
	if (!shelltexture)
		return false;
	if (!R_AliasShellTextureUnit(tex, shell_unit))
		return false;

	if (R_GetPowerupPickupShellColor(e, pickup_shell_color))
	{
		float shellAlpha;

		shellAlpha = R_PowerupShellTextureAlpha();
		if (shellAlpha <= 0.0001f)
			return false;

		ApplyShellEffect(glsl, pickup_shell_color[0], pickup_shell_color[1], pickup_shell_color[2], cl.time, shellAlpha, *shell_unit);
		return true;
	}

	if (e != &cl.viewent || chase_active.value)
		return false;

	if ((cl.time <= cl.faceanimtime || M_LivePreview_UseDamageTint()) && cl_damagehue.value)
	{
		if (e == &cl.viewent && !chase_active.value)
		{
			if (r_coloredpowerupglow.value)
			{
				float shellAlpha = R_PowerupShellTextureAlpha();
				if (shellAlpha <= 0.0001f)
					return false;
				plcolour_t dhvalue = CL_PLColours_Parse(cl_damagehuecolor.string);
				byte* dhuecolor = CL_PLColours_ToRGB(&dhvalue);
				
				// apply darker damage hue color to the shell
				float red = dhuecolor[0] / 255.0f * 0.7f;
				float green = dhuecolor[1] / 255.0f * 0.7f;
				float blue = dhuecolor[2] / 255.0f * 0.7f;
				
				ApplyShellEffect(glsl, red, green, blue, cl.time, shellAlpha, *shell_unit);
				return true;
			}
		}
	}
	else
	{
		if (e == &cl.viewent && !chase_active.value)
		{
			if (r_coloredpowerupglow.value)
			{
				float shellAlpha = R_PowerupShellTextureAlpha();
				if (shellAlpha <= 0.0001f)
					return false;

				if ((powerup_items & IT_QUAD) && (powerup_items & IT_INVULNERABILITY))
					ApplyShellEffect(glsl, 1.0f, 0.0f, 1.0f, cl.time, shellAlpha, *shell_unit);
				else if (powerup_items & IT_QUAD)
					ApplyShellEffect(glsl, 0.0f, 0.0f, 1.0f, cl.time, shellAlpha, *shell_unit);
				else if (powerup_items & IT_INVULNERABILITY)
					ApplyShellEffect(glsl, 1.0f, 0.0f, 0.0f, cl.time, shellAlpha, *shell_unit);
				else
					return false;
				return true;
			}
		}
	}

	return false;
}

static void R_RestoreAliasShellTextureState(aliasglsl_t* glsl, int shell_unit) // -- woods #powershell
{
	GL_Uniform1iFunc(glsl->useShellTexLoc, 0);
	if (shell_unit >= 0)
	{
		GL_SelectTexture(GL_TEXTURE0_ARB + shell_unit);
		GL_Bind(NULL);
	}
	GL_SelectTexture(GL_TEXTURE0);
}

/*
=============
R_EndAliasOutlineRendering -- woods #routline
=============
*/
void R_EndAliasOutlineRendering(void)
{
	// Restore the previous OpenGL state
	glDisable(GL_STENCIL_TEST);
	glPopAttrib();
}

/*
=============
GL_DrawAliasFrame_GLSL -- ericw

Optimized alias model drawing codepath.
Compared to the original GL_DrawAliasFrame, this makes 1 draw call,
no vertex data is uploaded (it's already in the r_meshvbo and r_meshindexesvbo
static VBOs), and lerping and lighting is done in the vertex shader.

Supports optional overbright, optional fullbright pixels.

Based on code by MH from RMQEngine
=============
*/
static void GL_DrawAliasFrame_GLSL (aliasglsl_t *glsl, aliashdr_t *paliashdr, lerpdata_t lerpdata, struct skintextures_s tex, entity_t *e)
{
	GLfloat	tints[3][4];
	float	blend;
	qboolean applied_shell;
	int shell_unit;

	if (!currententity->model) // woods -- flush guard
		return;

	if (lerpdata.pose1 != lerpdata.pose2)
	{
		blend = lerpdata.blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		blend = 0;
	}

	GL_UseProgramFunc (glsl->program);

	GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, currententity->model->meshindexesvbo);

	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2NormalAttrIndex);

	switch(paliashdr->poseverttype)
	{
	case PV_QUAKE1:
		GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0, currententity->model->meshvboptr+paliashdr->vbostofs);

		GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_mdl_t), GLARB_GetXYZOffset_MDL (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_mdl_t), GLARB_GetXYZOffset_MDL (paliashdr, lerpdata.pose2));
		// GL_TRUE to normalize the signed bytes to [-1 .. 1]
		GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_mdl_t), GLARB_GetNormalOffset_MDL (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_mdl_t), GLARB_GetNormalOffset_MDL (paliashdr, lerpdata.pose2));
		break;
	case PV_QUAKEFORGE:
		GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0, currententity->model->meshvboptr+paliashdr->vbostofs);

		GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_UNSIGNED_SHORT, GL_FALSE, sizeof (meshxyz_mdl16_t), GLARB_GetXYZOffset_MDLQF (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_UNSIGNED_SHORT, GL_FALSE, sizeof (meshxyz_mdl16_t), GLARB_GetXYZOffset_MDLQF (paliashdr, lerpdata.pose2));
		// GL_TRUE to normalize the signed bytes to [-1 .. 1]
		GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_mdl16_t), GLARB_GetNormalOffset_MDLQF (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_mdl16_t), GLARB_GetNormalOffset_MDLQF (paliashdr, lerpdata.pose2));
		break;
	case PV_QUAKE3:
		GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0, currententity->model->meshvboptr+paliashdr->vbostofs);

		GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_SHORT, GL_FALSE, sizeof (meshxyz_md3_t), GLARB_GetXYZOffset_MD3 (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_SHORT, GL_FALSE, sizeof (meshxyz_md3_t), GLARB_GetXYZOffset_MD3 (paliashdr, lerpdata.pose2));
		// GL_TRUE to normalize the signed bytes to [-1 .. 1]
		GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_md3_t), GLARB_GetNormalOffset_MD3 (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_md3_t), GLARB_GetNormalOffset_MD3 (paliashdr, lerpdata.pose2));
		break;
	case PV_IQM:
		{
			const iqmvert_t *pose = (const iqmvert_t*)(currententity->model->meshvboptr+paliashdr->vbovertofs + (paliashdr->numverts_vbo * 0 * sizeof (iqmvert_t)));

			GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 3, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->xyz);
			GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 3, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->norm);
			GL_VertexAttribPointerFunc (boneWeightAttrIndex, 4, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->weight);
			GL_VertexAttribPointerFunc (boneIndexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (iqmvert_t), pose->idx);
			GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->st);

			GL_EnableVertexAttribArrayFunc (vertColoursAttrIndex);
			GL_VertexAttribPointerFunc (vertColoursAttrIndex, 4, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->rgba);
		}
		break;
	}

#define MyVectorScale(a,s,b) do{(b)[0]=(s)*(a)[0];(b)[1]=(s)*(a)[1];(b)[2]=(s)*(a)[2];}while(0)
	if (cl.scores && e->netstate.colormap > 0 && e->netstate.colormap <= cl.maxclients)
	{
		scoreboard_t* sb = &cl.scores[e->netstate.colormap - 1];
		qboolean have_view_score = cl.viewentity > 0 && cl.viewentity <= cl.maxclients;
		qboolean isSamePants = false;
		qboolean isSelf = false;
		byte* pal;

		const char* enemycolor = gl_enemycolor.string;
		const char* teamcolor = gl_teamcolor.string;

		plcolour_t enemy = CL_PLColours_Parse(enemycolor);
		plcolour_t team = CL_PLColours_Parse(teamcolor);

		qboolean isTeamColorSet = strcmp(gl_teamcolor.string, "") != 0;
		qboolean isEnemyColorSet = strcmp(gl_enemycolor.string, "") != 0;
		if (have_view_score)
		{
			isSamePants = sb->pants.basic == cl.scores[cl.viewentity - 1].pants.basic;
			isSelf = sb->userinfo == CL_GetSafeViewEntityUserinfo();
		}

		if ((isTeamColorSet || isEnemyColorSet) && !cls.demoplayback && !isSelf && have_view_score && key_dest != key_menu) // woods #enemycolors, do we run it?
		{
			if (isTeamColorSet && !isEnemyColorSet && cl.teamcolor[0]) // team color active, enemy blank
			{
				if (isSamePants) // player has SAME color than me, set TEAM COLOR
				{
					pal = CL_PLColours_ToRGB(&team);
					MyVectorScale(pal, 1.0 / 255, tints[0]);
					MyVectorScale(pal, 1.0 / 255, tints[1]);
				}
				else
				{
					pal = CL_PLColours_ToRGB(&sb->pants);
					MyVectorScale(pal, 1.0 / 255, tints[0]);
					pal = CL_PLColours_ToRGB(&sb->shirt);
					MyVectorScale(pal, 1.0 / 255, tints[1]);
				}
			}

			if (isEnemyColorSet && !isTeamColorSet && cl.teamcolor[0]) // enemy color active, team blank
			{
				if (!isSamePants) // player has diff color than me, set ENEMY COLOR
				{
					pal = CL_PLColours_ToRGB(&enemy);
					MyVectorScale(pal, 1.0 / 255, tints[0]);
					MyVectorScale(pal, 1.0 / 255, tints[1]);
				}
				else
				{
					pal = CL_PLColours_ToRGB(&sb->pants);
					MyVectorScale(pal, 1.0 / 255, tints[0]);
					pal = CL_PLColours_ToRGB(&sb->shirt);
					MyVectorScale(pal, 1.0 / 255, tints[1]);
				}
			}

			if (isEnemyColorSet && isTeamColorSet && cl.teamcolor[0]) // both enemy and team active
			{
				if (!isSamePants) // player has diff color than me, set ENEMY COLOR
				{
					pal = CL_PLColours_ToRGB(&enemy);
					MyVectorScale(pal, 1.0 / 255, tints[0]);
					MyVectorScale(pal, 1.0 / 255, tints[1]);
				}
				if (isSamePants) // player has SAME color than me, set TEAM COLOR
				{
					pal = CL_PLColours_ToRGB(&team);
					MyVectorScale(pal, 1.0 / 255, tints[0]);
					MyVectorScale(pal, 1.0 / 255, tints[1]);
				}
			}

			if (isEnemyColorSet && !cl.teamcolor[0])// ffa all enemies green
			{
				pal = CL_PLColours_ToRGB(&enemy);
				MyVectorScale(pal, 1.0 / 255, tints[0]);
				MyVectorScale(pal, 1.0 / 255, tints[1]);
			}
			else if (!isEnemyColorSet && !cl.teamcolor[0])
			{
				pal = CL_PLColours_ToRGB(&sb->pants);
				MyVectorScale(pal, 1.0 / 255, tints[0]);
				pal = CL_PLColours_ToRGB(&sb->shirt);
				MyVectorScale(pal, 1.0 / 255, tints[1]);
			}

		}
		else // default case, no enemy or team colors on
		{
			pal = CL_PLColours_ToRGB(&sb->pants);
			MyVectorScale(pal, 1.0 / 255, tints[0]);
			pal = CL_PLColours_ToRGB(&sb->shirt);
			MyVectorScale(pal, 1.0 / 255, tints[1]);
		}
	}
	else
	{
		MyVectorScale((byte*)&d_8to24table[BOTTOM_RANGE+15], 1.0/255, tints[0]);
		MyVectorScale((byte*)&d_8to24table[TOP_RANGE+15], 1.0/255, tints[1]);
	}
	tints[0][3] = 1;
	tints[1][3] = 1;
	MyVectorScale(e->netstate.glowmod, 1.0/32, tints[2]); tints[2][3] = 1;

	// set textures
	GL_SelectTexture (GL_TEXTURE0);
	GL_Bind (tex.base);

	if (tex.luma)
	{
		GL_SelectTexture (GL_TEXTURE1);
		GL_Bind (tex.luma);
	}
	else
		tints[2][0] = tints[2][1] = tints[2][2] = tints[2][3] = 0;

	if (tex.lower)
	{
		GL_SelectTexture (GL_TEXTURE2);
		GL_Bind (tex.lower);
	}
	else
		tints[0][0] = tints[0][1] = tints[0][2] = tints[0][3] = 0;	//ask the glsl to not read it

	if (tex.upper)
	{
		GL_SelectTexture (GL_TEXTURE3);
		GL_Bind (tex.upper);
	}
	else
		tints[1][0] = tints[1][1] = tints[1][2] = tints[1][3] = 0;

// set uniforms
	if (glsl->blendLoc != -1)
		GL_Uniform1fFunc (glsl->blendLoc, blend);
	if (glsl->bonesLoc != -1)
		GL_Uniform4fvFunc (glsl->bonesLoc, paliashdr->numbones*3, lerpdata.bonestate->mat);
	GL_Uniform3fFunc (glsl->shadevectorLoc, shadevector[0], shadevector[1], shadevector[2]);
	GL_Uniform4fFunc (glsl->lightColorLoc, lightcolor[0], lightcolor[1], lightcolor[2], entalpha);	//this includes colormod
	GL_Uniform1iFunc (glsl->useFullbrightTexLoc, (tex.luma != NULL) ? 1 : 0);
	GL_Uniform1fFunc (glsl->useOverbrightLoc, overbright ? 1 : 0);
	GL_Uniform1iFunc (glsl->useAlphaTestLoc, (currententity->model->flags & MF_HOLEY) ? 1 : 0);
	GL_Uniform4fvFunc(glsl->colorTintLoc, countof(tints), tints[0]);	//colourmapping and glowmod.
	GL_Uniform1iFunc (glsl->fogModeLoc, Fog_GetMode());

	R_BeginAliasOutlineRendering(glsl); // woods #routline

	applied_shell = R_ApplyPowerupShellEffect(glsl, e, tex, &shell_unit); // woods #powershell

// draw
	glDrawElements (GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT, currententity->model->meshindexesvboptr+paliashdr->eboofs);
	if (applied_shell)
		R_RestoreAliasShellTextureState(glsl, shell_unit); // woods #powershell

	if (e != &cl.viewent)
	{
		R_DrawPowerupPickupShell(glsl, paliashdr, &lerpdata, e); // woods #powershell
		R_DrawAliasModelOutline(glsl, paliashdr, &lerpdata, e); // woods #routline
	}
	else if (cl.items & (IT_QUAD | IT_INVULNERABILITY))
		R_DrawViewmodelShell(glsl, paliashdr, &lerpdata, e); // woods #powershell

	R_EndAliasOutlineRendering(); // woods #routline

// clean up
	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (vertColoursAttrIndex);

	GL_UseProgramFunc (0);
	GL_SelectTexture (GL_TEXTURE0);

	rs_aliaspasses += paliashdr->numtris;
}

/*
=============
GL_DrawAliasFrame
-- johnfitz -- rewritten to support colored light, lerping, entalpha, multitexture, and r_drawflat
-- spike -- rewritten to use vertex arrays, which should be slightly faster thanks to less branches+gl calls (note that this requires gl1.1, which we depend on anyway for texture objects, and is pretty much universal.
=============
*/
void GL_DrawAliasFrame (aliashdr_t *paliashdr, lerpdata_t lerpdata)
{
	if (!currententity->model) // woods -- flush guard
		return;

	static	vec3_t vpos[65536];
	static	vec4_t vc[65536];
	int i;
	float	blend, iblend;
	const float *texcoords = (const float *)(currententity->model->meshvboptr+paliashdr->vbostofs);
	int texcoordstride = 0;
	qboolean use_viewmodel_stencil = false;
	GLuint viewmodel_stencil_bit = GL_VIEWMODEL_STENCIL_BIT();

	if (viewmodel_stencil_bit && gl_laserpoint.value && currententity == &cl.viewent)
	{
		glPushAttrib(GL_STENCIL_BUFFER_BIT | GL_ENABLE_BIT);
		glEnable(GL_STENCIL_TEST);
		glStencilMask(viewmodel_stencil_bit);
		glStencilFunc(GL_ALWAYS, (GLint)viewmodel_stencil_bit, (GLint)viewmodel_stencil_bit);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		use_viewmodel_stencil = true;
	}

	if (lerpdata.pose1 != lerpdata.pose2)
	{
		blend = lerpdata.blend;
		iblend = 1.0-blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		blend = 1;
		iblend = 0;
	}

	//pose1*iblend + pose2*blend

	if (shading && r_drawflat_cheatsafe)
	{
		shading = false;
		glColor4f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0, entalpha);
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	switch(paliashdr->poseverttype)
	{
	case PV_QUAKE1:
	case PV_QUAKEFORGE:	//just going to ignore the extra data here.
		{
			trivertx_t *verts1 = (trivertx_t*)((byte *)paliashdr + paliashdr->vertexes) + lerpdata.pose1 * paliashdr->numverts_vbo*(paliashdr->poseverttype==PV_QUAKEFORGE?2:1);
			trivertx_t *verts2 = (trivertx_t*)((byte *)paliashdr + paliashdr->vertexes) + lerpdata.pose2 * paliashdr->numverts_vbo*(paliashdr->poseverttype==PV_QUAKEFORGE?2:1);

			if (iblend)
			{
				for (i = 0; i < paliashdr->numverts_vbo; i++)
				{
					vpos[i][0] = verts1[i].v[0] * iblend + blend * verts2[i].v[0];
					vpos[i][1] = verts1[i].v[1] * iblend + blend * verts2[i].v[1];
					vpos[i][2] = verts1[i].v[2] * iblend + blend * verts2[i].v[2];
				}
				GL_BindBuffer (GL_ARRAY_BUFFER, 0);
				glVertexPointer(3, GL_FLOAT, sizeof (vpos[0]), vpos);

				if (shading)
				{
					for (i = 0; i < paliashdr->numverts_vbo; i++)
					{
						vc[i][0] = (shadedots[verts1->lightnormalindex]*iblend + shadedots[verts2->lightnormalindex]*blend) * lightcolor[0];
						vc[i][1] = (shadedots[verts1->lightnormalindex]*iblend + shadedots[verts2->lightnormalindex]*blend) * lightcolor[1];
						vc[i][2] = (shadedots[verts1->lightnormalindex]*iblend + shadedots[verts2->lightnormalindex]*blend) * lightcolor[2];
						vc[i][3] = entalpha;
					}
					glEnableClientState(GL_COLOR_ARRAY);
					glColorPointer(4, GL_FLOAT, sizeof(*vc), vc);
				}
			}
			else
			{
				if (shading)
				{
					for (i = 0; i < paliashdr->numverts_vbo; i++)
					{
						vc[i][0] = shadedots[verts2->lightnormalindex] * lightcolor[0];
						vc[i][1] = shadedots[verts2->lightnormalindex] * lightcolor[1];
						vc[i][2] = shadedots[verts2->lightnormalindex] * lightcolor[2];
						vc[i][3] = entalpha;
					}
					glEnableClientState(GL_COLOR_ARRAY);
					GL_BindBuffer (GL_ARRAY_BUFFER, 0);
					glColorPointer(4, GL_FLOAT, 0, vc);
				}

				//glVertexPointer may not take GL_UNSIGNED_BYTE, which means we can't use our vbos. attribute 0 MAY be vertex coords, but I don't want to depend on that.
				for (i = 0; i < paliashdr->numverts_vbo; i++)
				{
					vpos[i][0] = verts2[i].v[0];
					vpos[i][1] = verts2[i].v[1];
					vpos[i][2] = verts2[i].v[2];
				}
				GL_BindBuffer (GL_ARRAY_BUFFER, 0);
				glVertexPointer(3, GL_FLOAT, sizeof (vpos[0]), vpos);
			}
		}
		break;
	case PV_QUAKE3:
		{
			md3XyzNormal_t *verts1 = (md3XyzNormal_t*)((byte *)paliashdr + paliashdr->vertexes) + lerpdata.pose1 * paliashdr->numverts_vbo;
			md3XyzNormal_t *verts2 = (md3XyzNormal_t*)((byte *)paliashdr + paliashdr->vertexes) + lerpdata.pose2 * paliashdr->numverts_vbo;

			if (iblend)
			{
				for (i = 0; i < paliashdr->numverts_vbo; i++)
				{
					vpos[i][0] = verts1[i].xyz[0] * iblend + blend * verts2[i].xyz[0];
					vpos[i][1] = verts1[i].xyz[1] * iblend + blend * verts2[i].xyz[1];
					vpos[i][2] = verts1[i].xyz[2] * iblend + blend * verts2[i].xyz[2];
				}
				GL_BindBuffer (GL_ARRAY_BUFFER, 0);
				glVertexPointer(3, GL_FLOAT, sizeof (vpos[0]), vpos);

				if (shading)
				{
					for (i = 0; i < paliashdr->numverts_vbo; i++)
					{
						vec3_t n;
						float dot;
						// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
						// this introduces some error (less than 0.004), but the normals were very coarse
						// to begin with
						//this should be a table.
						float lat = (float)verts2[i].latlong[0] * (2 * M_PI)*(1.0 / 255.0);
						float lng = (float)verts2[i].latlong[1] * (2 * M_PI)*(1.0 / 255.0);
						n[0] = blend * cos ( lng ) * sin ( lat );
						n[1] = blend * sin ( lng ) * sin ( lat );
						n[2] = blend * cos ( lat );
						lat = (float)verts1[i].latlong[0] * (2 * M_PI)*(1.0 / 255.0);
						lng = (float)verts1[i].latlong[1] * (2 * M_PI)*(1.0 / 255.0);
						n[0] += iblend * cos ( lng ) * sin ( lat );
						n[1] += iblend * sin ( lng ) * sin ( lat );
						n[2] += iblend * cos ( lat );
						dot = DotProduct(n, shadevector);
						if (dot < 0.0)	//bizzare maths guessed by mh
							dot = 1.0 + dot * (13.0 / 44.0);
						else
							dot = 1.0 + dot;
						vc[i][0] = dot * lightcolor[0];
						vc[i][1] = dot * lightcolor[1];
						vc[i][2] = dot * lightcolor[2];
						vc[i][3] = entalpha;
					}
					glEnableClientState(GL_COLOR_ARRAY);
					glColorPointer(4, GL_FLOAT, 0, vc);
				}
			}
			else
			{
				if (shading)
				{
					for (i = 0; i < paliashdr->numverts_vbo; i++)
					{
						vec3_t n;
						float dot;
						// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
						// this introduces some error (less than 0.004), but the normals were very coarse
						// to begin with
						//this should be a table.
						float lat = (float)verts2[i].latlong[0] * (2 * M_PI)*(1.0 / 255.0);
						float lng = (float)verts2[i].latlong[1] * (2 * M_PI)*(1.0 / 255.0);
						n[0] = cos ( lng ) * sin ( lat );
						n[1] = sin ( lng ) * sin ( lat );
						n[2] = cos ( lat );
						dot = DotProduct(n, shadevector);
						if (dot < 0.0)	//bizzare maths guessed by mh
							dot = 1.0 + dot * (13.0 / 44.0);
						else
							dot = 1.0 + dot;
						vc[i][0] = dot * lightcolor[0];
						vc[i][1] = dot * lightcolor[1];
						vc[i][2] = dot * lightcolor[2];
						vc[i][3] = entalpha;
					}
					glEnableClientState(GL_COLOR_ARRAY);
					GL_BindBuffer (GL_ARRAY_BUFFER, 0);
					glColorPointer(4, GL_FLOAT, 0, vc);
				}
				GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->meshvbo);
				glVertexPointer(3, GL_SHORT, sizeof (meshxyz_md3_t), GLARB_GetXYZOffset_MD3 (paliashdr, lerpdata.pose2));
			}
		}
		break;

	case PV_IQM:
		{	//iqm does its blending using bones instead of verts, so we only have to care about one pose here
			int morphpose = 0;
			const iqmvert_t *verts2 = (const iqmvert_t*)((byte *)paliashdr + paliashdr->vertexes) + morphpose * paliashdr->numverts_vbo;
			const iqmvert_t *vboverts2 = (const iqmvert_t*)(currententity->model->meshvboptr+paliashdr->vbovertofs) + (paliashdr->numverts_vbo * morphpose);

			if (lerpdata.bonestate)
			{	//oh dear. its animated. and we don't have any glsl to animate it for us.
				bonepose_t pose;
				const bonepose_t *in;
				const float *xyz;
				float w;
				int j, k;
				for (i = 0; i < paliashdr->numverts_vbo; i++)
				{
					// Match the skeletal GLSL path: blend all four weights, then transform position and normal.
					in = lerpdata.bonestate + verts2[i].idx[0];
					w = verts2[i].weight[0];
					for (j = 0; j < 12; j++)
						pose.mat[j] = in->mat[j] * w;
					for (k = 1; k < 4; k++)
					{
						w = verts2[i].weight[k];
						if (!w)
							continue;
						in = lerpdata.bonestate + verts2[i].idx[k];
						for (j = 0; j < 12; j++)
							pose.mat[j] += in->mat[j] * w;
					}

					xyz = verts2[i].xyz;
					vpos[i][0] = xyz[0]*pose.mat[0] + xyz[1]*pose.mat[1] + xyz[2]*pose.mat[2] + pose.mat[3];
					vpos[i][1] = xyz[0]*pose.mat[4] + xyz[1]*pose.mat[5] + xyz[2]*pose.mat[6] + pose.mat[7];
					vpos[i][2] = xyz[0]*pose.mat[8] + xyz[1]*pose.mat[9] + xyz[2]*pose.mat[10] + pose.mat[11];

					if (shading)
					{
						vec3_t n;
						float dot;

						xyz = verts2[i].norm;
						n[0] = xyz[0]*pose.mat[0] + xyz[1]*pose.mat[1] + xyz[2]*pose.mat[2];
						n[1] = xyz[0]*pose.mat[4] + xyz[1]*pose.mat[5] + xyz[2]*pose.mat[6];
						n[2] = xyz[0]*pose.mat[8] + xyz[1]*pose.mat[9] + xyz[2]*pose.mat[10];
						VectorNormalize(n);

						dot = DotProduct(n, shadevector);
						if (dot < 0.0)	//bizzare maths guessed by mh
							dot = 1.0 + dot * (13.0 / 44.0);
						else
							dot = 1.0 + dot;
						vc[i][0] = dot * lightcolor[0] * verts2[i].rgba[0];
						vc[i][1] = dot * lightcolor[1] * verts2[i].rgba[1];
						vc[i][2] = dot * lightcolor[2] * verts2[i].rgba[2];
						vc[i][3] = entalpha * verts2[i].rgba[3];
					}
				}

				if (shading)
				{
					glEnableClientState(GL_COLOR_ARRAY);
					GL_BindBuffer (GL_ARRAY_BUFFER, 0);
					glColorPointer(4, GL_FLOAT, 0, vc);
				}

				GL_BindBuffer (GL_ARRAY_BUFFER, 0);
				glVertexPointer(3, GL_FLOAT, 0, vpos);
			}
			else
			{
				if (shading)
				{
					for (i = 0; i < paliashdr->numverts_vbo; i++)
					{
						float dot;
						dot = DotProduct(verts2[i].norm, shadevector);
						if (dot < 0.0)	//bizzare maths guessed by mh
							dot = 1.0 + dot * (13.0 / 44.0);
						else
							dot = 1.0 + dot;
						vc[i][0] = dot * lightcolor[0] * verts2[i].rgba[0];
						vc[i][1] = dot * lightcolor[1] * verts2[i].rgba[1];
						vc[i][2] = dot * lightcolor[2] * verts2[i].rgba[2];
						vc[i][3] = entalpha * verts2[i].rgba[3];
					}
					glEnableClientState(GL_COLOR_ARRAY);
					GL_BindBuffer (GL_ARRAY_BUFFER, 0);
					glColorPointer(4, GL_FLOAT, 0, vc);
				}

				GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->meshvbo);
				glVertexPointer(3, GL_FLOAT, sizeof (iqmvert_t), vboverts2->xyz);
			}
			texcoordstride = sizeof(iqmvert_t);
			texcoords = vboverts2->st;
		}
		break;
	}

// set textures
	GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->meshvbo);
	if (mtexenabled)
	{
		GL_ClientActiveTextureFunc (GL_TEXTURE0);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, texcoordstride, texcoords);

		GL_ClientActiveTextureFunc (GL_TEXTURE1);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, texcoordstride, texcoords);
	}
	else
	{
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, texcoordstride, texcoords);
	}

// draw
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, currententity->model->meshindexesvbo);
	glDrawElements (GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT, currententity->model->meshindexesvboptr + paliashdr->eboofs);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);

	GL_BindBuffer (GL_ARRAY_BUFFER, 0);

// clean up
	if (mtexenabled)
	{
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		GL_ClientActiveTextureFunc (GL_TEXTURE0);
	}
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	if (use_viewmodel_stencil)
		glPopAttrib();

	rs_aliaspasses += paliashdr->numtris;
}

/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping
=================
*/
void R_SetupAliasFrame (aliashdr_t *paliashdr, entity_t *e, lerpdata_t *lerpdata)
{
	int frame = e->frame;
	int				posenum, numposes;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	if (e->lerpflags & LERP_EXPLICIT)
	{
		int frame2 = e->lerp.snap.frame2;
		float frac = e->lerp.snap.lerpfrac;
		float time = cl.time;

		if ((frame2 >= paliashdr->numframes) || (frame2 < 0))
			frame2 = 0;

		if (paliashdr->frames[(frac > 0.5)?frame2:frame].numposes > 1)
		{	//our stronger sequence is a framegroup, but we can only do two-way blends.
			if (frac > 0.5)
			{
				frame = frame2;
				time = e->lerp.snap.time[1];
			}
			else
				time = e->lerp.snap.time[0];

			if (time < 0)
				time = 0;	//just in case...
			frac = (time / paliashdr->frames[frame].interval);
			posenum = (int)frac;
			lerpdata->blend = (frac - posenum);

			posenum += paliashdr->frames[frame].firstpose;
			numposes = paliashdr->frames[frame].numposes;

			lerpdata->pose1 = (posenum)%numposes;
			lerpdata->pose2 = (posenum+1)%numposes;
		}
		else
		{
			lerpdata->blend = frac;

			lerpdata->pose1 = paliashdr->frames[frame].firstpose;
			lerpdata->pose1 += (unsigned int)(e->lerp.snap.time[0]/paliashdr->frames[frame].interval) % paliashdr->frames[frame].numposes;

			lerpdata->pose2 = paliashdr->frames[frame2].firstpose;
			lerpdata->pose2 += (unsigned int)(e->lerp.snap.time[1]/paliashdr->frames[frame2].interval) % paliashdr->frames[frame].numposes;
		}
	}
	else
	{
		posenum = paliashdr->frames[frame].firstpose;
		numposes = paliashdr->frames[frame].numposes;

		if (numposes > 1)
		{
			float time = cl.time + e->syncbase;	//Spike: Readded syncbase
			if (time < 0)
				time = 0;	//just in case...
			e->lerp.state.lerptime = paliashdr->frames[frame].interval;	//FIXME: no per-frame intervals
			posenum += (int)(time / e->lerp.state.lerptime) % numposes;
		}
		else
			e->lerp.state.lerptime = 0.1;

		if (e->lerpflags & LERP_WRESET) // woods #wplerp weapon change reset
		{
			e->lerp.state.lerpstart = 0;
			e->lerp.state.previouspose = 0;
			e->lerp.state.currentpose = 0;
			e->lerpflags -= LERP_WRESET;
		}

		if (e->lerpflags & LERP_RESETANIM) //kill any lerp in progress
		{
			e->lerp.state.lerpstart = 0;
			e->lerp.state.previouspose = posenum;
			e->lerp.state.currentpose = posenum;
			e->lerpflags -= LERP_RESETANIM;
		}
		else if (e->lerp.state.currentpose != posenum) // pose changed, start new lerp
		{
			if (e->lerpflags & LERP_RESETANIM2) //defer lerping one more time
			{
				e->lerp.state.lerpstart = 0;
				e->lerp.state.previouspose = posenum;
				e->lerp.state.currentpose = posenum;
				e->lerpflags -= LERP_RESETANIM2;
			}
			else
			{
				e->lerp.state.lerpstart = cl.time;
				e->lerp.state.previouspose = e->lerp.state.currentpose;
				e->lerp.state.currentpose = posenum;
			}
		}

		//set up values
		if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value < 2)) // woods lerp 3 for smooth sng etc, change != to < 2 #lerp3
		{
			float s = (cls.demoplayback && cls.demospeed < 0.f) ? -1.f : 1.f; // woods (iw) #democontrols
			if (e->lerpflags & LERP_FINISH && numposes == 1)
				lerpdata->blend = CLAMP (0.0f, (float)(cl.time - e->lerp.state.lerpstart) / (e->lerpfinish - e->lerp.state.lerpstart), 1.0f);
			else
				lerpdata->blend = CLAMP (0.0f, (float)(cl.time - e->lerp.state.lerpstart) / e->lerp.state.lerptime * s, 1.0f); // woods (iw) #democontrols
			if (lerpdata->blend == 1.0f)
				e->lerp.state.previouspose = e->lerp.state.currentpose;
			lerpdata->pose1 = e->lerp.state.previouspose;
			lerpdata->pose2 = e->lerp.state.currentpose;
		}
		else //don't lerp
		{
			lerpdata->blend = 1;
			lerpdata->pose1 = posenum;
			lerpdata->pose2 = posenum;
		}
	}

	if (paliashdr->numboneposes)
	{
		static bonepose_t inverted[256];
		bonepose_t lerpbones[256], l;
		int b, j;
		const boneinfo_t *bi = (const boneinfo_t *)((byte*)paliashdr + paliashdr->boneinfo);
		const bonepose_t *p1 = (const bonepose_t *)((byte*)paliashdr + paliashdr->boneposedata) + lerpdata->pose1*paliashdr->numbones;
		const bonepose_t *p2 = (const bonepose_t *)((byte*)paliashdr + paliashdr->boneposedata) + lerpdata->pose2*paliashdr->numbones;
		float w2 = lerpdata->blend;
		float w1 = 1-w2;

		if ((size_t)paliashdr->numbones > countof(lerpbones))
			Sys_Error ("%s has too many bones", e->model->name);

		for (b = 0; b < paliashdr->numbones; b++, p1++, p2++)
		{
			//interpolate it
			for (j = 0; j < 12; j++)
				l.mat[j] = p1->mat[j]*w1 + p2->mat[j]*w2;
			//concat it onto the parent (relative->abs)
			if (bi[b].parent < 0)
				memcpy(lerpbones[b].mat, l.mat, sizeof(l.mat));
			else
				R_ConcatTransforms((void*)lerpbones[bi[b].parent].mat, (void*)l.mat, (void*)lerpbones[b].mat);
			//and finally invert it
			R_ConcatTransforms((void*)lerpbones[b].mat, (void*)bi[b].inverse.mat, (void*)inverted[b].mat);
		}
		lerpdata->bonestate = inverted;	//and now we can use it.
	}
	else
		lerpdata->bonestate = NULL;
}

/*
=================
R_SetupEntityTransform -- johnfitz -- set up transform part of lerpdata
=================
*/
void R_SetupEntityTransform (entity_t *e, lerpdata_t *lerpdata)
{
	float blend;
	vec3_t d;
	int i;

	// if LERP_RESETMOVE, kill any lerps in progress
	if (e->lerpflags & LERP_RESETMOVE)
	{
		e->movelerpstart = 0;
		VectorCopy (e->origin, e->previousorigin);
		VectorCopy (e->origin, e->currentorigin);
		VectorCopy (e->angles, e->previousangles);
		VectorCopy (e->angles, e->currentangles);
		e->lerpflags -= LERP_RESETMOVE;
	}
	else if (!VectorCompare (e->origin, e->currentorigin) || !VectorCompare (e->angles, e->currentangles)) // origin/angles changed, start new lerp
	{
		e->movelerpstart = cl.time;
		VectorCopy (e->currentorigin, e->previousorigin);
		VectorCopy (e->origin,  e->currentorigin);
		VectorCopy (e->currentangles, e->previousangles);
		VectorCopy (e->angles,  e->currentangles);
	}

	//set up values
	if (r_lerpmove.value && e != &cl.viewent && e->lerpflags & LERP_MOVESTEP)
	{
		float s = (cls.demoplayback && cls.demospeed < 0.f) ? -1.f : 1.f; // woods (iw) #democontrols
		if (e->lerpflags & LERP_FINISH)
			blend = CLAMP (0.0f, (float)(cl.time - e->movelerpstart) / (e->lerpfinish - e->movelerpstart), 1.0f);
		else
			blend = CLAMP (0.0f, (float)(cl.time - e->movelerpstart) / 0.1f * s, 1.0f); // woods (iw) #democontrols

		//translation
		VectorSubtract (e->currentorigin, e->previousorigin, d);
		lerpdata->origin[0] = e->previousorigin[0] + d[0] * blend;
		lerpdata->origin[1] = e->previousorigin[1] + d[1] * blend;
		lerpdata->origin[2] = e->previousorigin[2] + d[2] * blend;

		//rotation
		VectorSubtract (e->currentangles, e->previousangles, d);
		for (i = 0; i < 3; i++)
		{
			if (d[i] > 180)  d[i] -= 360;
			if (d[i] < -180) d[i] += 360;
		}
		lerpdata->angles[0] = e->previousangles[0] + d[0] * blend;
		lerpdata->angles[1] = e->previousangles[1] + d[1] * blend;
		lerpdata->angles[2] = e->previousangles[2] + d[2] * blend;
	}
	else //don't lerp
	{
		VectorCopy (e->origin, lerpdata->origin);
		VectorCopy (e->angles, lerpdata->angles);
	}

	if (e->model && CL_ApplyModelRotation(e, lerpdata->angles, host_frametime)) // woods #clmrotate
	{
		e->effects &= ~EF_ROTATE; // EF_ROTATE already cleared server-side, but if mapper forgot
	}

	{
		vec3_t off;
		if (Chase_GetPlayerModelAngleOffset (e, off))
		{
			lerpdata->angles[PITCH] = anglemod (lerpdata->angles[PITCH] + off[PITCH]);
			lerpdata->angles[YAW]   = anglemod (lerpdata->angles[YAW]   + off[YAW]);
			lerpdata->angles[ROLL]  = anglemod (lerpdata->angles[ROLL]  + off[ROLL]);
		}
	}
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
void R_SetupAliasLighting (entity_t	*e)
{
	vec3_t		dist;
	float		add;
	int			i;
	int		quantizedangle;
	float		radiansangle;
	float		*origin;

	plcolour_t dhvalue = CL_PLColours_Parse(cl_damagehuecolor.string); // woods #damage
	byte* dhuecolor = CL_PLColours_ToRGB(&dhvalue); // woods #damage

	if (!r_refdef.drawworld)
		lightcolor[0] = lightcolor[1] = lightcolor[2] = 255;
	else
	{
		if (e->eflags & EFLAGS_VIEWMODEL)
		{
			origin = r_refdef.vieworg;
			R_LightPoint (origin);
		}
		else
		{
			origin = e->origin;
            R_LightPointCachedAlias (e, origin, e->model ? e->model->maxs[2] * 0.5f : 0.0f);
		}

		//add dlights
		for (i=0 ; i<MAX_DLIGHTS ; i++)
		{
			if (cl_dlights[i].die >= cl.time || (cl_dlights[i].spawn > cl.mtime[0] && cls.demoplayback)) // woods (iw) #democontrols
			{
				VectorSubtract (origin, cl_dlights[i].origin, dist);
				add = cl_dlights[i].radius - VectorLength(dist);
				if (add > 0)
					VectorMA (lightcolor, add, cl_dlights[i].color, lightcolor);
			}
		}
		
		// woods #dedat -- for models on _list:  1 = greyscale: keep brightness, no hue, 2 = white-full: force` neutral shading
		// viewmodel handling: -1 = greyscale for all models except viewmodels, -2 = greyscale for viewmodels, white-full for all other models

		if (r_model_light_desat.value && e->model)
		{
			int desat_val = (int)r_model_light_desat.value;
			qboolean listed = !!(e->model->flags & MOD_DESATLISTED);

			/*  +1 / +2 → only models in list
			 *  -1 / -2 → list OR the view-model                              */
			if ((desat_val > 0 && listed)
				|| (desat_val < 0 && (listed || e == &cl.viewent)))
			{
				int effect_type = 0;

				if (desat_val > 0)                       /* +1 | +2 */
					effect_type = desat_val;
				else if (desat_val == -1 && e != &cl.viewent)
					effect_type = 1;                       /* world greyscale */
				else if (desat_val == -2)
					effect_type = (e == &cl.viewent) ? 1   /* view-model grey */
					: 2;   /* others white   */

				if (effect_type)
		{
					switch (effect_type)
			{
					case 1: {   /* greyscale: drop hue, keep intensity */
						float intensity = (lightcolor[0] + lightcolor[1] + lightcolor[2]) * (1.0f / 3.0f);
						lightcolor[0] = lightcolor[1] = lightcolor[2] = intensity;
						break;
			}
					case 2:      /* white full-bright */
						lightcolor[0] = lightcolor[1] = lightcolor[2] = 255.0f;
						break;
		}
				}
			}
		}

		// minimum light value on gun (24)
		if (e->eflags & EFLAGS_VIEWMODEL)
		{
			add = 72.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
				if (add > 0.0f)
				{
					lightcolor[0] += add / 3.0f;
					lightcolor[1] += add / 3.0f;
					lightcolor[2] += add / 3.0f;
				}
			}

		// minimum light value on players (8)
		if (e > cl.entities && e <= cl.entities + cl.maxclients)
			{
			add = 24.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
				if (add > 0.0f)
				{
					lightcolor[0] += add / 3.0f;
					lightcolor[1] += add / 3.0f;
					lightcolor[2] += add / 3.0f;
				}
			}
		}
	
	// begin woods for hue damage taken #damage

	if ((cl.time <= cl.faceanimtime || M_LivePreview_UseDamageTint()) && cl_damagehue.value)
		if (e == &cl.viewent)
		{
			{
 				lightcolor[0] = dhuecolor[0];
				lightcolor[1] = dhuecolor[1];
				lightcolor[2] = dhuecolor[2];
			}
		}

	// end woods for damage taken

	// begin woods add hue to gun model with powerups, simple #powershell value of 1 to 2

	if (!((cl.time <= cl.faceanimtime || M_LivePreview_UseDamageTint()) && cl_damagehue.value))
	{
		if (r_coloredpowerupglow.value && gl_powerupshells.value)
		{
			int powerup_items = cl.items | (M_LivePreview_UsePowerupShells() ? IT_QUAD : 0);

			if (e == &cl.viewent && (powerup_items & (IT_QUAD | IT_INVULNERABILITY)))
			{
			float alpha;
				float t;

			if (gl_powerupshells.value <= 1.0f)
			{
				if (shelltexture)
				{
					// Original behavior for shell effect
						alpha = 0.95f;
				}
				else
				{
						// Treat 0..1 similarly to 1..2 for non-shell
						t = CLAMP(gl_powerupshells.value, 0.0f, 1.0f);
					alpha = t * t * (3.0f - 2.0f * t);
				}
			}
				else
			{
					// Map 1..2 -> 0..1 then ease
					t = CLAMP((gl_powerupshells.value - 1.0f) / (2.0f - 1.0f), 0.0f, 1.0f);
					alpha = t * t * (3.0f - 2.0f * t);
				}

				{
					const float kmax = 5.0f;
					float alpha_knob = CLAMP(0.0f, gl_powerupshells_alpha.value, 1.0f);
					float k = alpha_knob * kmax;
					float a = CLAMP(alpha, 0.0f, 1.0f);
					alpha = 1.0f - powf(1.0f - a, k);
				}

				// Pick a single tint to apply (no triple-blend)
				vec3_t tint = { 0, 0, 0 };
				if ((powerup_items & (IT_QUAD | IT_INVULNERABILITY)) == (IT_QUAD | IT_INVULNERABILITY))
				{
					// both
					tint[0] = 211.0f; tint[1] = 113.0f; tint[2] = 194.0f;
				}
				else if (powerup_items & IT_QUAD)
				{
					// quad
					tint[0] = 50.0f; tint[1] = 50.0f; tint[2] = 121.0f;
				}
				else
				{
					// invulnerability
					tint[0] = 131.0f; tint[1] = 73.0f; tint[2] = 73.0f;
				}

				// Blend toward tint in RGB
				lightcolor[0] = lightcolor[0] * (1.0f - alpha) + tint[0] * alpha;
				lightcolor[1] = lightcolor[1] * (1.0f - alpha) + tint[1] * alpha;
				lightcolor[2] = lightcolor[2] * (1.0f - alpha) + tint[2] * alpha;

				// Saturation push around luma. Reuse k derived from alpha knob (k=alpha*5).
				{
					const float kmax = 5.0f;
					float alpha_knob = CLAMP(0.0f, gl_powerupshells_alpha.value, 1.0f);
					float k = alpha_knob * kmax;
					float Y = 0.299f * lightcolor[0] + 0.587f * lightcolor[1] + 0.114f * lightcolor[2];
					float satmul = 1.0f + (k - 1.0f);

					lightcolor[0] = Y + (lightcolor[0] - Y) * satmul;
					lightcolor[1] = Y + (lightcolor[1] - Y) * satmul;
					lightcolor[2] = Y + (lightcolor[2] - Y) * satmul;

					lightcolor[0] = CLAMP(0.0f, lightcolor[0], 255.0f);
					lightcolor[1] = CLAMP(0.0f, lightcolor[1], 255.0f);
					lightcolor[2] = CLAMP(0.0f, lightcolor[2], 255.0f);
				}
				}
		}
	}

	// end woods add hue to gun model with powerups

	// clamp lighting so it doesn't overbright as much (96)
	if (overbright)
	{
		add = 288.0f / (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add < 1.0f)
			VectorScale(lightcolor, add, lightcolor);
	}

	//hack up the brightness when fullbrights but no overbrights (256)
	if (gl_fullbrights.value && !gl_overbright_models.value)
		if (e->model->flags & MOD_FBRIGHTHACK)
		{
			lightcolor[0] = 256.0f;
			lightcolor[1] = 256.0f;
			lightcolor[2] = 256.0f;
		}

	quantizedangle = ((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1);

//ericw -- shadevector is passed to the shader to compute shadedots inside the
//shader, see GLAlias_CreateShaders()
	radiansangle = (quantizedangle / 16.0) * 2.0 * 3.14159;
	shadevector[0] = cos(-radiansangle);
	shadevector[1] = sin(-radiansangle);
	shadevector[2] = 1;
	VectorNormalize(shadevector);
//ericw --

	shadedots = r_avertexnormal_dots[quantizedangle];
	VectorScale (lightcolor, 1.0f / 200.0f, lightcolor);

	lightcolor[0] *= e->netstate.colormod[0] / 32.0;
	lightcolor[1] *= e->netstate.colormod[1] / 32.0;
	lightcolor[2] *= e->netstate.colormod[2] / 32.0;
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel (entity_t *e)
{
	aliasglsl_t *glsl;
	aliashdr_t	*paliashdr;
	int		anim, skinnum;
	struct skintextures_s tex;
	lerpdata_t	lerpdata;
	qboolean	alphatest = !!(e->model->flags & MF_HOLEY);
	int surf;
	float		fovscale = 1.0f;
	qmodel_t* clmodel = currententity->model;   // woods lightning alpha #lightalpha & doubleeyes 

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	glsl = &r_alias_glsl[(paliashdr->poseverttype==PV_IQM&&lerpdata.bonestate)?ALIAS_GLSL_SKELETAL:ALIAS_GLSL_BASIC];

	if (e->eflags & EFLAGS_VIEWMODEL)
	{
		if (skyroom_drawing)
			return;	//no viewmodels inside skyrooms!

		//transform it relative to the view, by rebuilding the modelview matrix without the view position.
		glPushMatrix ();
		glLoadIdentity();
		glRotatef (-90,  1, 0, 0);	    // put Z going up
		glRotatef (90,  0, 0, 1);	    // put Z going up

		glDepthRange (0, 0.3);

		//FIXME: this needs to go. combine with depthrange and explicit viewmodel-only fov into a different projection matrix..
		if (r_refdef.basefov > 90.f && cl_gun_fovscale.value)
			fovscale = 1.0f / tan(DEG2RAD(r_refdef.basefov / 2.0)) / cl_gun_fovscale.value; // woods
	}
	else
	{
		//
		// cull it
		//
		if (R_CullModelForEntityTransform(e, lerpdata.origin, lerpdata.angles))
			return;

		//
		// transform it
		//
		glPushMatrix ();
	}

	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e);

	// woods added doubleeyes (MH)

	if (!strcmp(clmodel->name, "progs/eyes.mdl") /*&& gl_doubleeyes.value*/)
	{	// gl_doubleeyes fix by mh Tue Sep 25, 2012 5:00 pm 
	// scaling factor - gl_doubleeyes 0 = unscaled, gl_doubleeyes 1 = 2x
		float sc = 1 + 1.0f;

		// offsets for eyes.mdl derived by taking the scaled midpoint of all verts in the mdl
		// you may wish to calculate these at load time rather than hard-code them in the engine
		float ofs[3] = { -0.13172054 * 1, -0.078105450 * 1, 25.347622 * 1 };

		// matrix for scaling and positioning the eyes
		float eyematrix[16] = { sc, 0, 0, 0, 0, sc, 0, 0, 0, 0, sc, 0, -ofs[0], -ofs[1], -ofs[2], 1 };

		// and fix things up
		glMultMatrixf(eyematrix);

		glTranslatef(paliashdr->scale_origin[0] * fovscale, paliashdr->scale_origin[1], paliashdr->scale_origin[2]); // woods
		glScalef(paliashdr->scale[0] * fovscale, paliashdr->scale[1], paliashdr->scale[2]);
	}
	else
	{
		glTranslatef(paliashdr->scale_origin[0] * fovscale, paliashdr->scale_origin[1], paliashdr->scale_origin[2]); // woods
		glScalef(paliashdr->scale[0] * fovscale, paliashdr->scale[1], paliashdr->scale[2]);
	}

	// end double eyes / woods

	if (!strcmp(clmodel->name, "progs/bolt.mdl") ||
		!strcmp(clmodel->name, "progs/bolt2.mdl") ||
		!strcmp(clmodel->name, "progs/bolt3.mdl"))   // woods for lighting alpha #lightalpha
	{
		float lightning_alpha = R_LightningAlphaForModel(clmodel);
		currententity->alpha = (lightning_alpha <= 0.0f) ? ENTALPHA_ZERO : ENTALPHA_ENCODE(lightning_alpha);
	}
	
	//
	// random stuff
	//
	if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
		glShadeModel (GL_SMOOTH);
	if (gl_affinemodels.value)
		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	overbright = gl_overbright_models.value;
	shading = true;

	//
	// set up for alpha blending
	//
	if (r_drawflat_cheatsafe || r_lightmap_cheatsafe) //no alpha in drawflat or lightmap mode
		entalpha = 1;
	else
		entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0)
		goto cleanup;
	if (entalpha < 1)
	{
		if (!gl_texture_env_combine) overbright = false; //overbright can't be done in a single pass without combiners
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
	}
	else if (alphatest)
		glEnable (GL_ALPHA_TEST);
	if (e->effects & EF_ADDITIVE)
	{
		glBlendFunc (GL_SRC_ALPHA, GL_ONE);
		glEnable(GL_BLEND);
	}

	//
	// set up lighting
	//
	{
		vec3_t off;
		float saved_yaw = e->angles[YAW];
		qboolean chase_offset = Chase_GetPlayerModelAngleOffset (e, off);
		if (chase_offset)
			e->angles[YAW] = anglemod (e->angles[YAW] + off[YAW]);
		R_SetupAliasLighting (e);
		if (chase_offset)
			e->angles[YAW] = saved_yaw;
	}

	for(surf=0;;surf++)
	{
		struct gltexture_s *colormapped_base = NULL;

		rs_aliaspolys += paliashdr->numtris;

		//
		// set up textures
		//
		GL_DisableMultitexture();
		anim = (int)(cl.time*10) & 3;
		skinnum = e->skinnum;
		if ((skinnum >= paliashdr->numskins) || (skinnum < 0))
		{
			Con_DPrintf ("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum, e->model->name);
			// ericw -- display skin 0 for winquake compatibility
			skinnum = 0;
		}
		if (paliashdr->numskins <= 0)
		{
			tex.base = tex.luma = tex.lower = tex.upper = NULL; // NULL will give the checkerboard texture
		}
		else
			tex = paliashdr->textures[skinnum][anim];
		if (!gl_nocolors.value)
		{
			if (e->eflags & EFLAGS_COLOURMAPPED)
			{	//support for dp's self.colormap = 4096 | top*16 | bottom; thing (solves corpses changing colours, can't handle rich colours though)
				if (tex.base && tex.base->source_format == SRC_INDEXED && !tex.upper && !tex.lower)
				{
					struct gltexture_s *t = TexMgr_ColormapTexture(tex.base, CL_PLColours_FromLegacy(e->netstate.colormap&15), CL_PLColours_FromLegacy(e->netstate.colormap>>4));
					if (t)
					{
						tex.base = t;
						colormapped_base = t;
					}
				}
			}
			else if (cl.scores && e->netstate.colormap>=1&&e->netstate.colormap<=cl.maxclients)
			{	//despite being able to handle _shirt+_pants textures in our glsl, we still prefer to generate per-player textures.
				//1) works with non-glsl.
				//2) preserves the weird non-linear ranges.
				//3) ... and without breaking those ranges on models that are NOT colourmapped (the lower's remapped range is the worst of the non-linear ranges, so would make too many models ugly).
				//so we only use the shirt+pans stuff when using external textures
				//on the plus side, we do use a lookup so we don't break quakerally. csqc also benefits from not needing to worry about edict numbers.
				if (tex.base && tex.base->source_format == SRC_INDEXED && !tex.upper && !tex.lower)
				{
					scoreboard_t* sb = &cl.scores[e->netstate.colormap - 1];
					qboolean have_view_score = cl.viewentity > 0 && cl.viewentity <= cl.maxclients;
					qboolean isSamePants = false;
					qboolean isSelf = false;
					struct gltexture_s* t = NULL; // woods

					// woods force colors #enemycolors

					plcolour_t enemy = CL_PLColours_Parse(gl_enemycolor.string);
					plcolour_t team = CL_PLColours_Parse(gl_teamcolor.string);

					qboolean isTeamColorSet = strcmp(gl_teamcolor.string, "") != 0;
					qboolean isEnemyColorSet = strcmp(gl_enemycolor.string, "") != 0;
					if (have_view_score)
					{
						isSamePants = sb->pants.basic == cl.scores[cl.viewentity - 1].pants.basic;
						isSelf = sb->userinfo == CL_GetSafeViewEntityUserinfo();
					}

					if ((isTeamColorSet || isEnemyColorSet) && !cls.demoplayback && !isSelf && have_view_score && key_dest != key_menu) // woods #enemycolors, do we run it?
					{
						if (isTeamColorSet && !isEnemyColorSet) // team color active, enemy blank
							t = isSamePants ? TexMgr_ColormapTexture(tex.base, team, team) : TexMgr_ColormapTexture(tex.base, sb->pants, sb->shirt);
						else if (!isTeamColorSet && isEnemyColorSet) // enemy color active, team blank
							t = !isSamePants ? TexMgr_ColormapTexture(tex.base, enemy, enemy) : TexMgr_ColormapTexture(tex.base, sb->pants, sb->shirt);
						else if (isTeamColorSet && isEnemyColorSet) // both enemy and team active
							t = !isSamePants ? TexMgr_ColormapTexture(tex.base, enemy, enemy) : TexMgr_ColormapTexture(tex.base, team, team);

						if (isEnemyColorSet && !cl.teamcolor[0]) // ffa, all players are enemies! 
							t = TexMgr_ColormapTexture(tex.base, enemy, enemy);
						else if (!isEnemyColorSet && !cl.teamcolor[0])
							t = TexMgr_ColormapTexture(tex.base, sb->pants, sb->shirt);
					}
					else // default case, no enemy or team colors on
						t = TexMgr_ColormapTexture(tex.base, sb->pants, sb->shirt);

					if (t)
					{
						tex.base = t;
						colormapped_base = t;
					}
				}
			}
		}
		if (!gl_fullbrights.value)
			tex.luma = NULL;

		// woods #md5crash -- the skin/colormap work above can call TexMgr_ColormapTexture,
		// which allocates from the hunk; if that grows a new hunk segment it triggers a
		// Cache_Flush that frees the very model we are drawing (alias extradata lives in the
		// relocatable cache). Re-fetch our surface pointer so the draw below and the surface
		// walk never dereference freed memory.
		{
			aliashdr_t *old_paliashdr = paliashdr;
			struct skintextures_s old_tex = tex;
			struct skintextures_s fresh_tex;
			aliashdr_t *rebase = (aliashdr_t *)Mod_Extradata (e->model);
			int rs;

			if (!rebase)
				goto cleanup;
			paliashdr = rebase;
			for (rs = 0; rs < surf && paliashdr->nextsurface; rs++)
				paliashdr = (aliashdr_t*)((byte*)paliashdr + paliashdr->nextsurface);

			if (paliashdr->numskins <= 0)
				fresh_tex.base = fresh_tex.luma = fresh_tex.lower = fresh_tex.upper = NULL;
			else
				fresh_tex = paliashdr->textures[skinnum][anim];

			if (colormapped_base)
				fresh_tex.base = colormapped_base;
			if (!gl_fullbrights.value)
				fresh_tex.luma = NULL;

			if (old_paliashdr != paliashdr ||
				old_tex.base != fresh_tex.base ||
				old_tex.luma != fresh_tex.luma ||
				old_tex.lower != fresh_tex.lower ||
				old_tex.upper != fresh_tex.upper)
			{
				Con_DPrintf("R_DrawAliasModel: refreshed alias textures after cache rebase: model=%s surf=%d skin=%d anim=%d hdr=%p->%p base=%p->%p luma=%p->%p lower=%p->%p upper=%p->%p%s\n",
					e->model->name, surf, skinnum, anim,
					(void *)old_paliashdr, (void *)paliashdr,
					(void *)old_tex.base, (void *)fresh_tex.base,
					(void *)old_tex.luma, (void *)fresh_tex.luma,
					(void *)old_tex.lower, (void *)fresh_tex.lower,
					(void *)old_tex.upper, (void *)fresh_tex.upper,
					colormapped_base ? " preserved_colormap" : "");
				if (fresh_tex.base)
					Con_DPrintf("R_DrawAliasModel: fresh base texture name=%s texnum=%u owner=%s\n",
						fresh_tex.base->name, fresh_tex.base->texnum,
						fresh_tex.base->owner ? fresh_tex.base->owner->name : "<none>");
			}

			tex = fresh_tex;
		}

		//
		// draw it
		//
		if (r_drawflat_cheatsafe)
		{
			glDisable (GL_TEXTURE_2D);
			GL_DrawAliasFrame (paliashdr, lerpdata);
			glEnable (GL_TEXTURE_2D);
			srand((int) (cl.time * 1000)); //restore randomness
		}
		else if (r_fullbright_cheatsafe)
		{
			GL_Bind (tex.base);
			shading = false;
			glColor4f(1,1,1,entalpha);
			GL_DrawAliasFrame (paliashdr, lerpdata);
			if (tex.luma)
			{
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_Bind(tex.luma);
				glEnable(GL_BLEND);
				glBlendFunc (GL_ONE, GL_ONE);
				glDepthMask(GL_FALSE);
				glColor3f(entalpha,entalpha,entalpha);
				Fog_StartAdditive ();
				GL_DrawAliasFrame (paliashdr, lerpdata);
				Fog_StopAdditive ();
				glDepthMask(GL_TRUE);
				glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable(GL_BLEND);
			}
		}
		else if (r_lightmap_cheatsafe)
		{
			glDisable (GL_TEXTURE_2D);
			shading = false;
			glColor3f(1,1,1);
			GL_DrawAliasFrame (paliashdr, lerpdata);
			glEnable (GL_TEXTURE_2D);
		}
	// call fast path if possible. if the shader compliation failed for some reason,
	// r_alias_program will be 0.
		else if (glsl->program != 0 && (paliashdr->numbones <= glsl->maxbones||!lerpdata.bonestate))
		{
			GL_DrawAliasFrame_GLSL (glsl, paliashdr, lerpdata, tex, e);
		}
		else if (overbright)
		{
			if  (gl_texture_env_combine && gl_mtexable && gl_texture_env_add && tex.luma) //case 1: everything in one pass
			{
				GL_Bind (tex.base);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
				glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
				GL_EnableMultitexture(); // selects TEXTURE1
				GL_Bind (tex.luma);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
//				glEnable(GL_BLEND);
				GL_DrawAliasFrame (paliashdr, lerpdata);
//				glDisable(GL_BLEND);
				GL_DisableMultitexture();
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
			else if (gl_texture_env_combine) //case 2: overbright in one pass, then fullbright pass
			{
			// first pass
				GL_Bind(tex.base);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
				glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
				GL_DrawAliasFrame (paliashdr, lerpdata);
				glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			// second pass
				if (tex.luma)
				{
					glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
					GL_Bind(tex.luma);
					glEnable(GL_BLEND);
					glBlendFunc (GL_ONE, GL_ONE);
					glDepthMask(GL_FALSE);
					shading = false;
					glColor3f(entalpha,entalpha,entalpha);
					Fog_StartAdditive ();
					GL_DrawAliasFrame (paliashdr, lerpdata);
					Fog_StopAdditive ();
					glDepthMask(GL_TRUE);
					glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					glDisable(GL_BLEND);
					glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				}
			}
			else //case 3: overbright in two passes, then fullbright pass
			{
			// first pass
				GL_Bind(tex.base);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_DrawAliasFrame (paliashdr, lerpdata);
			// second pass -- additive with black fog, to double the object colors but not the fog color
				glEnable(GL_BLEND);
				glBlendFunc (GL_ONE, GL_ONE);
				glDepthMask(GL_FALSE);
				Fog_StartAdditive ();
				GL_DrawAliasFrame (paliashdr, lerpdata);
				Fog_StopAdditive ();
				glDepthMask(GL_TRUE);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable(GL_BLEND);
			// third pass
				if (tex.luma)
				{
					glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
					GL_Bind(tex.luma);
					glEnable(GL_BLEND);
					glBlendFunc (GL_ONE, GL_ONE);
					glDepthMask(GL_FALSE);
					shading = false;
					glColor3f(entalpha,entalpha,entalpha);
					Fog_StartAdditive ();
					GL_DrawAliasFrame (paliashdr, lerpdata);
					Fog_StopAdditive ();
					glDepthMask(GL_TRUE);
					glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					glDisable(GL_BLEND);
					glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				}
			}
		}
		else
		{
			if (gl_mtexable && gl_texture_env_add && tex.luma) //case 4: fullbright mask using multitexture
			{
				GL_DisableMultitexture(); // selects TEXTURE0
				GL_Bind (tex.base);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_EnableMultitexture(); // selects TEXTURE1
				GL_Bind (tex.luma);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
				glEnable(GL_BLEND);
				GL_DrawAliasFrame (paliashdr, lerpdata);
				glDisable(GL_BLEND);
				GL_DisableMultitexture();
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
			else //case 5: fullbright mask without multitexture
			{
			// first pass
				GL_Bind(tex.base);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_DrawAliasFrame (paliashdr, lerpdata);
			// second pass
				if (tex.luma)
				{
					GL_Bind(tex.luma);
					glEnable(GL_BLEND);
					glBlendFunc (GL_ONE, GL_ONE);
					glDepthMask(GL_FALSE);
					shading = false;
					glColor3f(entalpha,entalpha,entalpha);
					Fog_StartAdditive ();
					GL_DrawAliasFrame (paliashdr, lerpdata);
					Fog_StopAdditive ();
					glDepthMask(GL_TRUE);
					glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					glDisable(GL_BLEND);
				}
			}
		}
		if (!paliashdr->nextsurface)
			break;
		{
			intptr_t ns = paliashdr->nextsurface;
			if (ns < 256 || ns > 64*1024*1024)	// woods #md5crash -- guard against corrupted surface chains
			{
				static qboolean warned = false;
				if (!warned)
				{
					warned = true;
					Con_Warning("R_DrawAliasModel: '%s' bad nextsurface %lld at surf %d (pvt %d, numbones %d, numtris %d) - stopping walk\n",
						e->model->name, (long long)ns, surf, (int)paliashdr->poseverttype, paliashdr->numbones, paliashdr->numtris);
				}
				break;
			}
			paliashdr = (aliashdr_t*)((byte*)paliashdr + ns);
		}
	}

cleanup:
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glShadeModel (GL_FLAT);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	if (alphatest)
		glDisable (GL_ALPHA_TEST);
	glColor3f(1,1,1);
	if (e->eflags & EFLAGS_VIEWMODEL)
		glDepthRange (0, 1);
	if (e->effects & EF_ADDITIVE)
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPopMatrix ();
}

//johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.7 //skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y 0 //skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0 //0=completely flat
#define SHADOW_HEIGHT 0.1 //how far above the floor to render the shadow
//johnfitz

#define SHADOW_COMPUTED (1 << 0) // woods #shadow
#define SHADOW_VALID    (1 << 1) // woods #shadow

qboolean GL_DrawAliasShadowCheck (entity_t* e) // woods check for shadows not on the ground (ultrav quad, ctf2m8 quad, etc) #shadow
{
	const float shadowRadius = 55.0f;  // Distance from the entity to cast the shadow
	const float traceDistance = 50.0f; // How far down to trace

	vec3_t start, end, impact;

	VectorCopy(e->origin, start); // Set the starting point as the entity's origin shifted based on SHADOW_SKEW_X (westward in this case)
	start[0] += SHADOW_SKEW_X * shadowRadius; // Skew along x-axis (westward)

	VectorCopy(start, end); // Set the ending point directly below the starting point
	end[2] -= traceDistance;  // Trace directly downwards

	TraceLine(start, end, 0, impact); // Perform the trace to check if there's ground below

	if (VectorCompare(impact, end)) // If the trace hits the maximum distance, there is no ground, so no shadow
	{
		e->shadow_state |= SHADOW_COMPUTED;  // Mark as computed
		e->shadow_state &= ~SHADOW_VALID;    // Shadow is not valid
		return false;  // No ground detected, skip shadow
	}

	e->shadow_state |= (SHADOW_COMPUTED | SHADOW_VALID);  // Mark as computed and valid
	return true;
}

/*
=============
GL_DrawAliasShadow_GLSL -- woods #shadowglsl

Shadow variant of GL_DrawAliasFrame_GLSL: one draw call from the static mesh
VBOs with pose lerping in the vertex shader, instead of the fixed-function
path's per-vertex CPU lerp into client arrays (which stalls badly on Apple's
GL-on-Metal driver). LightColor rgb is 0 so the skin multiplies out to black;
binding the real skin keeps MF_HOLEY alpha holes in the shadow.
=============
*/
static void GL_DrawAliasShadow_GLSL (aliasglsl_t *glsl, aliashdr_t *paliashdr, lerpdata_t lerpdata, entity_t *e, float shadowalpha)
{
	const GLfloat zerotints[3][4] = {{0,0,0,0},{0,0,0,0},{0,0,0,0}};
	float	blend;
	int		anim, skinnum;
	gltexture_t *skin;

	if (lerpdata.pose1 != lerpdata.pose2)
		blend = lerpdata.blend;
	else
		blend = 0;

	// Only HOLEY models need the real skin (so the alpha test can punch the
	// model's transparent texels out of the shadow). For everything else bind an
	// opaque texture: the frag shader does result.a *= texture.a, so a skin whose
	// base texture carries <1 alpha (soft-alpha md3/md5 skins) would otherwise
	// modulate the shadow's opacity. notexture is 2x2 fully-opaque; its RGB is
	// irrelevant here because LightColor.rgb is 0.
	if (e->model->flags & MF_HOLEY)
	{
		anim = (int)(cl.time*10) & 3;
		skinnum = e->skinnum;
		if ((skinnum >= paliashdr->numskins) || (skinnum < 0))
			skinnum = 0;
		if (paliashdr->numskins <= 0)
			skin = notexture;
		else
			skin = paliashdr->textures[skinnum][anim].base;
	}
	else
		skin = notexture;

	GL_UseProgramFunc (glsl->program);

	GL_BindBuffer (GL_ARRAY_BUFFER, e->model->meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, e->model->meshindexesvbo);

	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2NormalAttrIndex);

	switch(paliashdr->poseverttype)
	{
	case PV_QUAKE1:
		GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0, e->model->meshvboptr+paliashdr->vbostofs);

		GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_mdl_t), GLARB_GetXYZOffset_MDL (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_mdl_t), GLARB_GetXYZOffset_MDL (paliashdr, lerpdata.pose2));
		// GL_TRUE to normalize the signed bytes to [-1 .. 1]
		GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_mdl_t), GLARB_GetNormalOffset_MDL (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_mdl_t), GLARB_GetNormalOffset_MDL (paliashdr, lerpdata.pose2));
		break;
	case PV_QUAKEFORGE:
		GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0, e->model->meshvboptr+paliashdr->vbostofs);

		GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_UNSIGNED_SHORT, GL_FALSE, sizeof (meshxyz_mdl16_t), GLARB_GetXYZOffset_MDLQF (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_UNSIGNED_SHORT, GL_FALSE, sizeof (meshxyz_mdl16_t), GLARB_GetXYZOffset_MDLQF (paliashdr, lerpdata.pose2));
		GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_mdl16_t), GLARB_GetNormalOffset_MDLQF (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_mdl16_t), GLARB_GetNormalOffset_MDLQF (paliashdr, lerpdata.pose2));
		break;
	case PV_QUAKE3:
		GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0, e->model->meshvboptr+paliashdr->vbostofs);

		GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_SHORT, GL_FALSE, sizeof (meshxyz_md3_t), GLARB_GetXYZOffset_MD3 (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_SHORT, GL_FALSE, sizeof (meshxyz_md3_t), GLARB_GetXYZOffset_MD3 (paliashdr, lerpdata.pose2));
		GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_md3_t), GLARB_GetNormalOffset_MD3 (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_md3_t), GLARB_GetNormalOffset_MD3 (paliashdr, lerpdata.pose2));
		break;
	case PV_IQM:
		{
			const iqmvert_t *pose = (const iqmvert_t*)(e->model->meshvboptr+paliashdr->vbovertofs + (paliashdr->numverts_vbo * 0 * sizeof (iqmvert_t)));

			GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 3, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->xyz);
			GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 3, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->norm);
			GL_VertexAttribPointerFunc (boneWeightAttrIndex, 4, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->weight);
			GL_VertexAttribPointerFunc (boneIndexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (iqmvert_t), pose->idx);
			GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->st);

			GL_EnableVertexAttribArrayFunc (vertColoursAttrIndex);
			GL_VertexAttribPointerFunc (vertColoursAttrIndex, 4, GL_FLOAT, GL_FALSE, sizeof (iqmvert_t), pose->rgba);
		}
		break;
	}

	GL_SelectTexture (GL_TEXTURE0);
	GL_Bind (skin);

	if (glsl->blendLoc != -1)
		GL_Uniform1fFunc (glsl->blendLoc, blend);
	if (glsl->bonesLoc != -1)
		GL_Uniform4fvFunc (glsl->bonesLoc, paliashdr->numbones*3, lerpdata.bonestate->mat);
	GL_Uniform3fFunc (glsl->shadevectorLoc, shadevector[0], shadevector[1], shadevector[2]);
	// the fragment shader applies gl_Color.a twice (result *= gl_Color, then
	// result.a *= gl_Color.a), so pass the square root to land on shadowalpha
	GL_Uniform4fFunc (glsl->lightColorLoc, 0, 0, 0, sqrt(shadowalpha));
	GL_Uniform1iFunc (glsl->useFullbrightTexLoc, 0);
	GL_Uniform1fFunc (glsl->useOverbrightLoc, 0);
	GL_Uniform1iFunc (glsl->useAlphaTestLoc, (e->model->flags & MF_HOLEY) ? 1 : 0);
	GL_Uniform4fvFunc (glsl->colorTintLoc, 3, (const GLfloat*)zerotints);
	GL_Uniform1iFunc (glsl->fogModeLoc, Fog_GetMode());
	GL_Uniform1fFunc (glsl->outlineWidthLoc, 0.0f);
	GL_Uniform1iFunc (glsl->isOutlinePassLoc, 0);
	GL_Uniform1iFunc (glsl->useShellTexLoc, 0);
	GL_Uniform1iFunc (glsl->shellModeLoc, 0);

	glDrawElements (GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT, e->model->meshindexesvboptr+paliashdr->eboofs);

	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (vertColoursAttrIndex);

	GL_UseProgramFunc (0);
	GL_SelectTexture (GL_TEXTURE0);

	rs_aliaspasses += paliashdr->numtris;
}

/*
=============
GL_DrawAliasShadow -- johnfitz -- rewritten

TODO: orient shadow onto "lightplane" (a global mplane_t*)
=============
*/
void GL_DrawAliasShadow (entity_t *e)
{
	float	shadowmatrix[16] = {1,				0,				0,				0,
								0,				1,				0,				0,
								SHADOW_SKEW_X,	SHADOW_SKEW_Y,	SHADOW_VSCALE,	0,
								0,				0,				SHADOW_HEIGHT,	1};
	float		lheight;
	aliashdr_t	*paliashdr;
	lerpdata_t	lerpdata;
	float shade; // woods (R00k) : fade light based on ambientlight
	aliasglsl_t	*glsl; // woods #shadowglsl
	float		shadowalpha; // woods #shadowglsl

	if (e == &cl.viewent || e->effects & EF_NOSHADOW || e->model->flags & MOD_NOSHADOW)
		return;

	entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha < 1) return; // woods no shadows on transparent entities

	if (r_shadows_groundcheck.value && e->model->flags & EF_ROTATE) // woods #shadow
	{
		if (!(e->shadow_state & SHADOW_COMPUTED))
			GL_DrawAliasShadowCheck(e);

		if (!(e->shadow_state & SHADOW_VALID))
			return;
	}

	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	if (R_CullModelForEntityTransform(e, lerpdata.origin, lerpdata.angles))
		return;

	R_LightPointCachedAlias (e, e->origin, 0); // woods #shadowglsl -- reuse the static-entity light cache instead of re-tracing
	shade = (((lightcolor[0] + lightcolor[1] + lightcolor[2]) / 3) / 128); // woods (R00k) : fade light based on ambientlight
	lheight = currententity->origin[2] - lightspot[2];

// set up matrix
	glPushMatrix ();
	glTranslatef (lerpdata.origin[0],  lerpdata.origin[1],  lerpdata.origin[2]);
	glTranslatef (0,0,-lheight);
	glMultMatrixf (shadowmatrix);
	glTranslatef (0,0,lheight);
	glRotatef (lerpdata.angles[1],  0, 0, 1);
	glRotatef (-lerpdata.angles[0],  0, 1, 0);
	glRotatef (lerpdata.angles[2],  1, 0, 0);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

// draw it
	glDepthMask(GL_FALSE);
	glEnable (GL_BLEND);
	GL_DisableMultitexture ();
	shadowalpha = CLAMP (0.0f, entalpha * shade * r_shadows.value, 1.0f); // woods (R00k) : fade light based on ambientlight
	glsl = &r_alias_glsl[(paliashdr->poseverttype==PV_IQM&&lerpdata.bonestate)?ALIAS_GLSL_SKELETAL:ALIAS_GLSL_BASIC]; // woods #shadowglsl
	if (glsl->program != 0 && (paliashdr->numbones <= glsl->maxbones || !lerpdata.bonestate))
	{
		GL_DrawAliasShadow_GLSL (glsl, paliashdr, lerpdata, e, shadowalpha);
	}
	else
	{
		glDisable (GL_TEXTURE_2D);
		shading = false;
		glColor4f(0,0,0,shadowalpha);
		GL_DrawAliasFrame (paliashdr, lerpdata);
		glEnable (GL_TEXTURE_2D);
	}
	glDisable (GL_BLEND);
	glDepthMask(GL_TRUE);

//clean up
	glPopMatrix ();
}

/*
=================
R_DrawAliasModel_ShowTris -- johnfitz
=================
*/
void R_DrawAliasModel_ShowTris (entity_t *e)
{
	aliashdr_t	*paliashdr;
	lerpdata_t	lerpdata;
	float	fovscale = 1.0f;

	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	if (R_CullModelForEntityTransform(e, lerpdata.origin, lerpdata.angles))
		return;

	if (e == &cl.viewent && r_refdef.basefov > 90.f && cl_gun_fovscale.value)
		fovscale = 1.0f / tan(DEG2RAD(r_refdef.basefov / 2.0)) / cl_gun_fovscale.value;

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin,lerpdata.angles, e);
	glTranslatef (paliashdr->scale_origin[0] * fovscale, paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0] * fovscale, paliashdr->scale[1], paliashdr->scale[2]);

	shading = false;
	glColor3f(1,1,1);
	GL_DrawAliasFrame (paliashdr, lerpdata);

	glPopMatrix ();
}
