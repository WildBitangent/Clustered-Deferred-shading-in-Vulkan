#include "UI.h"
#include <GLFW/glfw3.h>
#include "BaseApp.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"

UI::UI(GLFWwindow* window, Renderer& renderer)
	: mRenderer(renderer)
{
	ImGui::CreateContext();
	setColorScheme();

	ImGui_ImplGlfw_InitForVulkan(window, true);

	initResources();
	createPipeline();
}

void UI::onKeyPress(int key, int action)
{
	// if (action == GLFW_PRESS)
	// {
	// 	switch (key)
	// 	{
	// 		// todo use a hash map or something
	// 	case GLFW_KEY_PAGE_UP:
	// 		++mDebugIndex;
	// 		mDebugUniformDirtyBit = true;
	// 		break;
	// 	case GLFW_KEY_PAGE_DOWN:
	// 		--mDebugIndex;
	// 		mDebugUniformDirtyBit = true;
	// 		break;
	//
	// 	case GLFW_KEY_ENTER:
	// 		BaseApp::getInstance().getRenderer().reloadShaders();
	// 		break;
	// 	}
	// }
}

DebugStates UI::getDebugIndex() const
{
	return mContext.debugState;
}

bool UI::debugStateUniformNeedsUpdate()
{
	auto ret = mContext.debugUniformDirtyBit;
	mContext.debugUniformDirtyBit = false;
	return ret;
}

void UI::update()
{
	// ImGui::ShowDemoWindow(nullptr);

	using namespace ImGui;

	if (Begin("Settings"))
	{
		Text("%.3f ms/frame (%.1f FPS)", 1000.0f / GetIO().Framerate, GetIO().Framerate);

		Checkbox("Light animation", &mContext.lightsAnimation);
		Checkbox("V-Sync", &mContext.vSync);

		if (TreeNode("Render texture"))
		{
			const auto names = { "Default", "Albedo", "Normal", "Specular", "Position", "Depth" };

			for (size_t i = 0; i < static_cast<size_t>(DebugStates::count); i++)
			{
				if (Selectable(*(names.begin() + i), static_cast<DebugStates>(i) == mContext.debugState))
				{
					mContext.debugState = static_cast<DebugStates>(i);
					mContext.debugUniformDirtyBit = true;
				}
			}
			TreePop();
		}

		if (TreeNode("Tile settings"))
		{
			constexpr int maxLights = 500'000; // todo refarctor this
			DragInt("Number of lights", &mContext.lightsCount, 10, 1, maxLights);

			// v_max doesn't work properly, make sure it doesn't exceed
			if (mContext.lightsCount > maxLights) mContext.lightsCount = maxLights;

			const char* options[] = { "8x8", "16x16", "32x32", "64x64" };
			if (Combo("Tile Size", &mContext.tileSize, options, IM_ARRAYSIZE(options)))
				mContext.shaderReloadDirtyBit = true;

			TreePop();
		}

		if (TreeNode("Light extents"))
		{
			DragFloat3("Min", reinterpret_cast<float*>(&mContext.lightBoundMin), 0.25);
			DragFloat3("Max", reinterpret_cast<float*>(&mContext.lightBoundMax), 0.25);

			TreePop();
		}
	}
	End();
}

void UI::resize()
{
	createPipeline();
}

void UI::copyDrawData()
{
	auto drawData = ImGui::GetDrawData();

	// todo check total idx/vertex data count
	auto vertexData = reinterpret_cast<ImDrawVert*>(mRenderer.mContext.getDevice().mapMemory(*mStagingBuffer.memory, 0, mStagingBuffer.size, {}));
	auto indexData = reinterpret_cast<ImDrawIdx*>(vertexData);
	
	for (size_t i = 0, dataCount = 0; i < drawData->CmdListsCount; i++)
	{
		auto& vertices = drawData->CmdLists[i]->VtxBuffer;
		memcpy(vertexData + dataCount, vertices.Data, vertices.Size * sizeof(ImDrawVert));
		dataCount += vertices.Size;
	}
	mRenderer.mUtility.copyBuffer(*mStagingBuffer.handle, *mVertexBuffer.handle, drawData->TotalVtxCount * sizeof(ImDrawVert), 0, 0);
	
	for (size_t i = 0, dataCount = 0; i < drawData->CmdListsCount; i++)
	{
		auto& indices = drawData->CmdLists[i]->IdxBuffer;
		memcpy(indexData + dataCount, indices.Data, indices.Size * sizeof(ImDrawIdx));
		dataCount += indices.Size;
	}
	mRenderer.mUtility.copyBuffer(*mStagingBuffer.handle, *mIndexBuffer.handle, drawData->TotalIdxCount * sizeof(ImDrawIdx), 0, 0);
	
	mRenderer.mContext.getDevice().unmapMemory(*mStagingBuffer.memory);
}

void UI::recordCommandBuffer()
{
	copyDrawData();

	auto& cmd = *mCmdBuffers[mCommandBufferInUse];
	auto& context = BaseApp::getInstance().getRenderer().mContext;
	const auto drawData = ImGui::GetDrawData();

	vk::CommandBufferInheritanceInfo inheritanceInfo;
	inheritanceInfo.renderPass = *BaseApp::getInstance().getRenderer().mCompositionRenderpass;
	inheritanceInfo.subpass = 1;

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue;
	beginInfo.pInheritanceInfo = &inheritanceInfo;

	auto& pipelineLayout = BaseApp::getInstance().getRenderer().mResource.pipelineLayout.get("UI");

	float scale[2];
	scale[0] = 2.0f / drawData->DisplaySize.x;
	scale[1] = 2.0f / drawData->DisplaySize.y;
	float translate[2];
	translate[0] = -1.0f - drawData->DisplayPos.x * scale[0];
	translate[1] = -1.0f - drawData->DisplayPos.y * scale[1];

	cmd.begin(beginInfo);
	cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(float) * 2, scale);
	cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex, sizeof(float) * 2, sizeof(float) * 2, translate);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, BaseApp::getInstance().getRenderer().mResource.pipeline.get("UI"));
	cmd.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		pipelineLayout,
		0,
		BaseApp::getInstance().getRenderer().mResource.descriptorSet.get("fontTexture"),
		nullptr
	);
	cmd.bindVertexBuffers(0, *mVertexBuffer.handle, { 0 });
	cmd.bindIndexBuffer(*mIndexBuffer.handle, 0, vk::IndexType::eUint32);
	
	size_t vertexOffset = 0;
	size_t indexOffset = 0;
	for (size_t i = 0; i < drawData->CmdListsCount; i++)
	{
		const auto cmdList = drawData->CmdLists[i];
		for (size_t n = 0; n < cmdList->CmdBuffer.Size; n++)
		{
			const auto pcmd = &cmdList->CmdBuffer[n];
			if (pcmd->UserCallback)
				pcmd->UserCallback(cmdList, pcmd);
			else
				vkCmdDrawIndexed(cmd, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);

			indexOffset += pcmd->ElemCount;
		}
		vertexOffset += cmdList->VtxBuffer.Size;
	}

	cmd.end();
}

vk::UniqueCommandBuffer& UI::getCommandBuffer()
{
	auto& ret = mCmdBuffers[mCommandBufferInUse];
	mCommandBufferInUse = (mCommandBufferInUse + 1) % 2; //todo mod swapchain img count
	return ret;
}

BufferParameters& UI::getVertexBuffer()
{
	return mVertexBuffer;
}

BufferParameters& UI::getIndexBuffer()
{
	return mIndexBuffer;
}

void UI::setColorScheme()
{
	ImGuiStyle& style = ImGui::GetStyle();
	style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.0f, 0.0f, 0.0f, 0.1f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.8f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
	style.Colors[ImGuiCol_Button] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
}

void UI::initResources()
{
	// alloc buffers
	mVertexBuffer = mRenderer.mUtility.createBuffer(
		10'000 * sizeof(ImDrawVert),
		vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	mIndexBuffer = mRenderer.mUtility.createBuffer(
		10'000 * sizeof(ImDrawIdx),
		vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	mStagingBuffer = mRenderer.mUtility.createBuffer(
		50'000 * 4,
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
	);

	// alloc cmd buffers
	vk::CommandBufferAllocateInfo cmdAllocInfo;
	cmdAllocInfo.commandPool = mRenderer.mContext.getDynamicCommandPool();
	cmdAllocInfo.level = vk::CommandBufferLevel::eSecondary;
	cmdAllocInfo.commandBufferCount = 2; // todo find out swapchain img count

	mCmdBuffers = mRenderer.mContext.getDevice().allocateCommandBuffersUnique(cmdAllocInfo);

	auto& io = ImGui::GetIO();

	// Create font texture
	unsigned char* fontData;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&fontData, &width, &height);
	vk::DeviceSize uploadSize = width * height * 4;

	// Create target image for copy
	mFontTexture = mRenderer.mUtility.createImage(
		width, height,
		vk::Format::eR8G8B8A8Unorm,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	// Image view
	mFontTexture.view = mRenderer.mUtility.createImageView(*mFontTexture.handle, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);

	// Staging buffers for font data upload
	BufferParameters stagingBuffer = mRenderer.mUtility.createBuffer(
		uploadSize,
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
	);

	auto stagingBufferData = mRenderer.mContext.getDevice().mapMemory(*stagingBuffer.memory, 0, stagingBuffer.size, {});
	memcpy(stagingBufferData, fontData, uploadSize);
	mRenderer.mContext.getDevice().unmapMemory(*stagingBuffer.memory);

	// Copy buffer data to font image
	auto cmd = mRenderer.mUtility.beginSingleTimeCommands();
	mRenderer.mUtility.recordTransitImageLayout(cmd, *mFontTexture.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	mRenderer.mUtility.recordCopyBuffer(cmd, *stagingBuffer.handle, *mFontTexture.handle, width, height);
	mRenderer.mUtility.recordTransitImageLayout(cmd, *mFontTexture.handle, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	mRenderer.mUtility.endSingleTimeCommands(cmd);

	// Font texture Sampler
	vk::SamplerCreateInfo samplerInfo;
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 1.0f;
	samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;

	mSampler = mRenderer.mContext.getDevice().createSamplerUnique(samplerInfo);

	// Descriptor set layout
	vk::DescriptorSetLayoutBinding samplerBinding;
	samplerBinding.binding = 0;
	samplerBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
	samplerBinding.descriptorCount = 1;
	samplerBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

	vk::DescriptorSetLayoutCreateInfo setLayoutCreateInfo;
	setLayoutCreateInfo.bindingCount = 1;
	setLayoutCreateInfo.pBindings = &samplerBinding;

	mRenderer.mResource.descriptorSetLayout.add("fontTexture", setLayoutCreateInfo);

	// Descriptor set
	vk::DescriptorSetAllocateInfo allocInfo;
	allocInfo.descriptorPool = *mRenderer.mDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &mRenderer.mResource.descriptorSetLayout.get("fontTexture");

	auto targetSet = mRenderer.mResource.descriptorSet.add("fontTexture", allocInfo);

	vk::DescriptorImageInfo imageInfo;
	imageInfo.sampler = *mSampler;
	imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	imageInfo.imageView = *mFontTexture.view;

	vk::WriteDescriptorSet writes;
	writes.dstSet = targetSet;
	writes.descriptorCount = 1;
	writes.dstBinding = 0;
	writes.descriptorType = vk::DescriptorType::eCombinedImageSampler;
	writes.pImageInfo = &imageInfo;

	mRenderer.mContext.getDevice().updateDescriptorSets(writes, nullptr);
}

void UI::createPipeline()
{
	// viewport
	vk::Viewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(mRenderer.mSwapchainExtent.width);
	viewport.height = static_cast<float>(mRenderer.mSwapchainExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vk::Rect2D scissor;
	scissor.offset = vk::Offset2D{ 0, 0 };
	scissor.extent = mRenderer.mSwapchainExtent;

	vk::PipelineViewportStateCreateInfo viewportInfo;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	vk::PipelineRasterizationStateCreateInfo rasterizer;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = vk::PolygonMode::eFill;
	rasterizer.lineWidth = 1.0f; // requires wideLines feature enabled when larger than one
	rasterizer.cullMode = vk::CullModeFlagBits::eNone;
	rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

	// no multisampling
	vk::PipelineMultisampleStateCreateInfo multisampling;
	multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
	
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
	inputAssemblyInfo.topology = vk::PrimitiveTopology::eTriangleList;

	// shader stages
	auto vertShader = mRenderer.mResource.shaderModule.add("data/ui.vert");
	auto fragShader = mRenderer.mResource.shaderModule.add("data/ui.frag");

	vk::PipelineShaderStageCreateInfo vertexStageInfo;
	vertexStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
	vertexStageInfo.module = vertShader;
	vertexStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo fragmentStageInfo;
	fragmentStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
	fragmentStageInfo.module = fragShader;
	fragmentStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, fragmentStageInfo };

	// vertex data info
	vk::VertexInputBindingDescription vertexBinding;
	vertexBinding.stride = sizeof(ImDrawVert);
	vertexBinding.inputRate = vk::VertexInputRate::eVertex;

	std::array<vk::VertexInputAttributeDescription, 3> attribDesc;
	attribDesc[0].location = 0;
	attribDesc[0].binding = vertexBinding.binding;
	attribDesc[0].format = vk::Format::eR32G32Sfloat;
	attribDesc[0].offset = IM_OFFSETOF(ImDrawVert, pos);
	attribDesc[1].location = 1;
	attribDesc[1].binding = vertexBinding.binding;
	attribDesc[1].format = vk::Format::eR32G32Sfloat;
	attribDesc[1].offset = IM_OFFSETOF(ImDrawVert, uv);
	attribDesc[2].location = 2;
	attribDesc[2].binding = vertexBinding.binding;
	attribDesc[2].format = vk::Format::eR8G8B8A8Unorm;
	attribDesc[2].offset = IM_OFFSETOF(ImDrawVert, col);

	vk::PipelineVertexInputStateCreateInfo vertexInfo;
	vertexInfo.vertexBindingDescriptionCount = 1;
	vertexInfo.pVertexBindingDescriptions = &vertexBinding;
	vertexInfo.vertexAttributeDescriptionCount = attribDesc.size();
	vertexInfo.pVertexAttributeDescriptions = attribDesc.data();

	// depth and stencil
	vk::PipelineDepthStencilStateCreateInfo depthStencil;

	// blending
	vk::PipelineColorBlendAttachmentState colorblendAttachment;
	colorblendAttachment.blendEnable = true;
	colorblendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	colorblendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	colorblendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	colorblendAttachment.colorBlendOp = vk::BlendOp::eAdd;
	colorblendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	colorblendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	colorblendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

	vk::PipelineColorBlendStateCreateInfo blendingInfo;
	blendingInfo.attachmentCount = 1;
	blendingInfo.pAttachments = &colorblendAttachment;

	std::vector<vk::DescriptorSetLayout> setLayouts = {
		mRenderer.mResource.descriptorSetLayout.get("fontTexture"),
	};

	vk::PushConstantRange pushConstants;
	pushConstants.stageFlags = vk::ShaderStageFlagBits::eVertex;
	pushConstants.size = sizeof(float) * 4;

	vk::PipelineLayoutCreateInfo layoutInfo;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	layoutInfo.pSetLayouts = setLayouts.data();
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushConstants;

	auto layout = mRenderer.mResource.pipelineLayout.add("UI", layoutInfo);

	vk::GraphicsPipelineCreateInfo pipelineInfo;
	pipelineInfo.flags = vk::PipelineCreateFlagBits::eDerivative;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInfo;
	pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	pipelineInfo.pViewportState = &viewportInfo;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &blendingInfo;
	pipelineInfo.layout = layout;
	pipelineInfo.renderPass = *mRenderer.mCompositionRenderpass;
	pipelineInfo.subpass = 1;
	pipelineInfo.basePipelineHandle = mRenderer.mResource.pipeline.get("composition"); // derive from composition pipeline
	pipelineInfo.basePipelineIndex = -1;

	mRenderer.mResource.pipeline.add("UI", *mRenderer.mPipelineCache, pipelineInfo);
}
