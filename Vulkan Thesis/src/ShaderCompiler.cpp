#include "ShaderCompiler.h"
#include <fstream>
#include <experimental/filesystem>

#include <glslang/public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <StandAlone/DirStackFileIncluder.h>
#include <iostream>

//using namespace shdc;

const TBuiltInResource defaultTBuiltInResource = 
{
	/* .MaxLights = */ 32,
	/* .MaxClipPlanes = */ 6,
	/* .MaxTextureUnits = */ 32,
	/* .MaxTextureCoords = */ 32,
	/* .MaxVertexAttribs = */ 64,
	/* .MaxVertexUniformComponents = */ 4096,
	/* .MaxVaryingFloats = */ 64,
	/* .MaxVertexTextureImageUnits = */ 32,
	/* .MaxCombinedTextureImageUnits = */ 80,
	/* .MaxTextureImageUnits = */ 32,
	/* .MaxFragmentUniformComponents = */ 4096,
	/* .MaxDrawBuffers = */ 32,
	/* .MaxVertexUniformVectors = */ 128,
	/* .MaxVaryingVectors = */ 8,
	/* .MaxFragmentUniformVectors = */ 16,
	/* .MaxVertexOutputVectors = */ 16,
	/* .MaxFragmentInputVectors = */ 15,
	/* .MinProgramTexelOffset = */ -8,
	/* .MaxProgramTexelOffset = */ 7,
	/* .MaxClipDistances = */ 8,
	/* .MaxComputeWorkGroupCountX = */ 65535,
	/* .MaxComputeWorkGroupCountY = */ 65535,
	/* .MaxComputeWorkGroupCountZ = */ 65535,
	/* .MaxComputeWorkGroupSizeX = */ 1024,
	/* .MaxComputeWorkGroupSizeY = */ 1024,
	/* .MaxComputeWorkGroupSizeZ = */ 64,
	/* .MaxComputeUniformComponents = */ 1024,
	/* .MaxComputeTextureImageUnits = */ 16,
	/* .MaxComputeImageUniforms = */ 8,
	/* .MaxComputeAtomicCounters = */ 8,
	/* .MaxComputeAtomicCounterBuffers = */ 1,
	/* .MaxVaryingComponents = */ 60,
	/* .MaxVertexOutputComponents = */ 64,
	/* .MaxGeometryInputComponents = */ 64,
	/* .MaxGeometryOutputComponents = */ 128,
	/* .MaxFragmentInputComponents = */ 128,
	/* .MaxImageUnits = */ 8,
	/* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
	/* .MaxCombinedShaderOutputResources = */ 8,
	/* .MaxImageSamples = */ 0,
	/* .MaxVertexImageUniforms = */ 0,
	/* .MaxTessControlImageUniforms = */ 0,
	/* .MaxTessEvaluationImageUniforms = */ 0,
	/* .MaxGeometryImageUniforms = */ 0,
	/* .MaxFragmentImageUniforms = */ 8,
	/* .MaxCombinedImageUniforms = */ 8,
	/* .MaxGeometryTextureImageUnits = */ 16,
	/* .MaxGeometryOutputVertices = */ 256,
	/* .MaxGeometryTotalOutputComponents = */ 1024,
	/* .MaxGeometryUniformComponents = */ 1024,
	/* .MaxGeometryVaryingComponents = */ 64,
	/* .MaxTessControlInputComponents = */ 128,
	/* .MaxTessControlOutputComponents = */ 128,
	/* .MaxTessControlTextureImageUnits = */ 16,
	/* .MaxTessControlUniformComponents = */ 1024,
	/* .MaxTessControlTotalOutputComponents = */ 4096,
	/* .MaxTessEvaluationInputComponents = */ 128,
	/* .MaxTessEvaluationOutputComponents = */ 128,
	/* .MaxTessEvaluationTextureImageUnits = */ 16,
	/* .MaxTessEvaluationUniformComponents = */ 1024,
	/* .MaxTessPatchComponents = */ 120,
	/* .MaxPatchVertices = */ 32,
	/* .MaxTessGenLevel = */ 64,
	/* .MaxViewports = */ 16,
	/* .MaxVertexAtomicCounters = */ 0,
	/* .MaxTessControlAtomicCounters = */ 0,
	/* .MaxTessEvaluationAtomicCounters = */ 0,
	/* .MaxGeometryAtomicCounters = */ 0,
	/* .MaxFragmentAtomicCounters = */ 8,
	/* .MaxCombinedAtomicCounters = */ 8,
	/* .MaxAtomicCounterBindings = */ 1,
	/* .MaxVertexAtomicCounterBuffers = */ 0,
	/* .MaxTessControlAtomicCounterBuffers = */ 0,
	/* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
	/* .MaxGeometryAtomicCounterBuffers = */ 0,
	/* .MaxFragmentAtomicCounterBuffers = */ 1,
	/* .MaxCombinedAtomicCounterBuffers = */ 1,
	/* .MaxAtomicCounterBufferSize = */ 16384,
	/* .MaxTransformFeedbackBuffers = */ 4,
	/* .MaxTransformFeedbackInterleavedComponents = */ 64,
	/* .MaxCullDistances = */ 8,
	/* .MaxCombinedClipAndCullDistances = */ 8,
	/* .MaxSamples = */ 4,
	/* .limits = */ 
	{
		/* .nonInductiveForLoops = */ 1,
		/* .whileLoops = */ 1,
		/* .doWhileLoops = */ 1,
		/* .generalUniformIndexing = */ 1,
		/* .generalAttributeMatrixVectorIndexing = */ 1,
		/* .generalVaryingIndexing = */ 1,
		/* .generalSamplerIndexing = */ 1,
		/* .generalVariableIndexing = */ 1,
		/* .generalConstantMatrixVectorIndexing = */ 1,
	} 
};

EShLanguage getShaderStage(const std::string& filename)
{
	std::string extension = std::experimental::filesystem::path(filename).extension().string();

	if (extension == ".vert")
		return EShLangVertex;
	else if (extension == ".tesc")
		return EShLangTessControl;
	else if (extension == ".tese")
		return EShLangTessEvaluation;
	else if (extension == ".geom")
		return EShLangGeometry;
	else if (extension == ".frag")
		return EShLangFragment;
	else if (extension == ".comp")
		return EShLangCompute;
	else
		throw std::runtime_error("Invalid shader suffix: " + extension);
}

std::vector<uint32_t> compileShader(const std::string& filename)
{
	static bool initDummy = glslang::InitializeProcess();

	std::ifstream file(filename);

	if (!file.is_open())
		throw std::runtime_error("Failed to open shader file: " + filename);

	std::string inputGLSL((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	const char* inputString = inputGLSL.c_str();
	
	EShLanguage shaderType = getShaderStage(filename);

	glslang::TShader shader(shaderType); 
	shader.setStrings(&inputString, 1);
	shader.setEnvInput(glslang::EShSourceGlsl, shaderType, glslang::EShClientVulkan, 100);
	shader.setEnvClient(glslang::EShClientVulkan, glslang::EshTargetClientVersion::EShTargetVulkan_1_1);
	shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetLanguageVersion::EShTargetSpv_1_3);

	DirStackFileIncluder includer;
	includer.pushExternalLocalDirectory(std::experimental::filesystem::path(filename).parent_path().string());

	EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
	std::string preprocessedGLSL;
	if (!shader.preprocess(&defaultTBuiltInResource, 110, ENoProfile, false, false, messages, &preprocessedGLSL, includer))
		throw std::runtime_error("Failed to preprocess file: " + filename);
		// TODO LOG

	const char* preprocessedCstr = preprocessedGLSL.c_str();
	shader.setStrings(&preprocessedCstr, 1);

	if (!shader.parse(&defaultTBuiltInResource, 110, false, messages))
	{
		std::string log = "Failed to parse file: " + filename + "\n";
		log += shader.getInfoLog();		//TODO make LOGGER
		log += shader.getInfoDebugLog();

		throw std::runtime_error(log);
	}

	glslang::TProgram program;
	program.addShader(&shader);
	
	if (!program.link(messages))
	{
		std::cout << shader.getInfoLog() << std::endl;		//TODO make LOGGER
		std::cout << shader.getInfoDebugLog() << std::endl;

		throw std::runtime_error("Failed to link program: " + filename);
	}

	std::vector<uint32_t> spirV;
	spv::SpvBuildLogger logger;
	glslang::SpvOptions spvOptions;
	glslang::GlslangToSpv(*program.getIntermediate(shaderType), spirV, &logger, &spvOptions);

	return spirV;
}
