#pragma once
#include <string>
#include "Util.h"

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

class UI
{
public:
	struct Context
	{
		DebugStates debugState = DebugStates::disabled;
		bool debugUniformDirtyBit = false;
		bool shaderReloadDirtyBit = false;

		glm::vec3 lightBoundMin = {-31, -5.5, -37.5};
		glm::vec3 lightBoundMax = {18.5, 15.5, 16};
		int lightsCount = 32768;
		int tileSize = 1;
		bool lightsAnimation = false;
		bool vSync = true;
	} mContext;

public:
	UI(GLFWwindow* window, Renderer& renderer);

	DebugStates getDebugIndex() const;
	bool debugStateUniformNeedsUpdate();

	void update();
	void resize();
	void copyDrawData(vk::CommandBuffer& cmd);
	vk::CommandBuffer recordCommandBuffer(size_t cmdIndex);

	BufferParameters& getVertexBuffer();
	BufferParameters& getIndexBuffer();

private:
	void setColorScheme();
	void initResources();
	void createPipeline();
	
private:
	Renderer& mRenderer;

	BufferParameters mVertexBuffer;
	BufferParameters mIndexBuffer;
	BufferParameters mStagingBuffer;

	ImageParameters mFontTexture;
	vk::UniqueSampler mSampler;

	std::vector<vk::UniqueCommandBuffer> mCmdBuffers;
};