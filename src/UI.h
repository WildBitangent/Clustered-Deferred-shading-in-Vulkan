/**
 * @file 'UI.h'
 * @brief User interface handling
 * @copyright The MIT license 
 * @author Matej Karas
 */

#pragma once
#include <string>
#include "Util.h"
#include "Scene.h"

struct GLFWwindow;
class Renderer;

enum class DebugStates : unsigned
{
	disabled,
	albedo,
	normal,
	specular,
	position,
	depth,

	count
};

enum class CullingMethod : int
{
	noculling,
	tiled,
	clustered,
};

enum class WindowSize : unsigned
{
	_1024x726,
	_1920x1080,
	_2048x1080,
	_4096x2160,
};

class UI
{
public:
	struct Context
	{
		DebugStates debugState = DebugStates::disabled;
		CullingMethod cullingMethod = CullingMethod::clustered;
		WindowSize windowSize = WindowSize::_1920x1080;
		bool debugUniformDirtyBit = false;
		bool shaderReloadDirtyBit = false;
		bool sceneReload = false;
		bool cullingMethodChanged = false;

		glm::vec3 lightBoundMin;
		glm::vec3 lightBoundMax;
		int lightsCount = 10;
		float lightSpeed = 0.f;
		int tileSize = 1;
		int currentScene = 0;
		bool vSync = false;
	} mContext;

public:
	UI(GLFWwindow* window, Renderer& renderer);

	DebugStates getDebugIndex() const;
	bool debugStateUniformNeedsUpdate();

	void update();
	void resize();
	void copyDrawData(vk::CommandBuffer cmd);
	void recordCommandBuffer(vk::CommandBuffer cmd);

private:
	void setColorScheme();
	void initResources();
	void createPipeline();
	void setWindowSize(WindowSize size);
	
private:
	Renderer& mRenderer;

	BufferParameters mDrawBuffer;
	BufferParameters mStagingBuffer;

	ImageParameters mFontTexture;
	vk::UniqueSampler mSampler;
};