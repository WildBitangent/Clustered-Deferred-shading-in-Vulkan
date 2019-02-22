#include "Util.h"
#include "Context.h"

#include <fstream>
#include <experimental/filesystem>

#include <glslang/public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <SPIRV/disassemble.h>
#include <StandAlone/DirStackFileIncluder.h>
#include <iostream>

#include "lodepng.h"


namespace
{
	uint32_t findMemoryType(uint32_t typeFilter, const vk::MemoryPropertyFlags& properties, vk::PhysicalDevice physicalDevice)
	{
		auto memoryProperties = physicalDevice.getMemoryProperties();

		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
		{
			auto supportedType = (typeFilter & (1 << i)) != 0;
			auto supportedProperties = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;

			if (supportedType && supportedProperties)
				return i;
		}

		throw std::runtime_error("Failed to find suitable memory type");
	}

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
}

vk::VertexInputBindingDescription util::getVertexBindingDesciption()
{
	using util::Vertex;
	vk::VertexInputBindingDescription bindingDescription;
	bindingDescription.binding = 0; // index of the binding, defined in vertex shader
	bindingDescription.stride = sizeof(Vertex);
	bindingDescription.inputRate = vk::VertexInputRate::eVertex; // move to next data engty after each vertex
	return bindingDescription;
}

std::array<vk::VertexInputAttributeDescription, 5> util::getVertexAttributeDescriptions()
{
	using util::Vertex;
	
	std::array<vk::VertexInputAttributeDescription, 5> descriptions;
	descriptions[0].binding = 0;
	descriptions[0].location = 0;
	descriptions[0].format = vk::Format::eR32G32B32Sfloat;
	descriptions[0].offset = offsetof(Vertex, pos); //bytes of a member since beginning of struct
	descriptions[1].binding = 0;
	descriptions[1].location = 1;
	descriptions[1].format = vk::Format::eR32G32B32Sfloat;
	descriptions[1].offset = offsetof(Vertex, color); //bytes of a member since beginning of struct
	descriptions[2].binding = 0;
	descriptions[2].location = 2;
	descriptions[2].format = vk::Format::eR32G32Sfloat;
	descriptions[2].offset = offsetof(Vertex, texCoord);
	descriptions[3].binding = 0;
	descriptions[3].location = 3;
	descriptions[3].format = vk::Format::eR32G32B32Sfloat;
	descriptions[3].offset = offsetof(Vertex, normal);
	descriptions[4].binding = 0;
	descriptions[4].location = 4;
	descriptions[4].format = vk::Format::eR32G32B32Sfloat;
	descriptions[4].offset = offsetof(Vertex, tangent);

	return descriptions;
}

#include "ShaderResourceConfig.inl"
std::vector<uint32_t> util::compileShader(const std::string& filename)
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

	// std::ofstream outFile(filename + ".spv"); // TODO make spv folder or smth
	// spv::Disassemble(outFile, spirV);

	return spirV;
}

size_t util::Vertex::hash() const
{
	size_t seed = 0;
	seed ^= std::hash<glm::vec3>()(pos) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= std::hash<glm::vec3>()(color) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= std::hash<glm::vec2>()(texCoord) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= std::hash<glm::vec3>()(normal) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	return seed;
}

Utility::Utility(const Context& context)
	: mContext(context)
{
}

vk::SurfaceFormatKHR Utility::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
	// When free to choose format
	if (availableFormats.size() == 1 && availableFormats[0].format == vk::Format::eUndefined)
		return{ vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };

	for (const auto& format : availableFormats)
		// prefer 32bits RGBA color with SRGB support
		if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			return format;
	
	return availableFormats[0];
}

vk::PresentModeKHR Utility::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availableModes)
{
	for (const auto& mode : availableModes)
		if (mode == vk::PresentModeKHR::eMailbox)
			return mode;

	return vk::PresentModeKHR::eFifo;
}

vk::Extent2D Utility::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
{
	// The swap extent is the resolution of the swap chain images
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		return capabilities.currentExtent;

	auto currentExtent = capabilities.currentExtent;
	currentExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, currentExtent.width));
	currentExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, currentExtent.height));

	return currentExtent;
}

vk::Format Utility::findSupportedFormat(const std::vector<vk::Format>& formats, vk::ImageTiling tiling, vk::FormatFeatureFlags features)
{
	for (const vk::Format& format : formats)
	{
		auto props = mContext.getPhysicalDevice().getFormatProperties(format);

		if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
			return format;
		else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
			return format;
	}

	throw std::runtime_error("Failed to find supported format");
}

BufferParameters Utility::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memProp)
{
	BufferParameters retObject;
	retObject.size = size;

	vk::BufferCreateInfo bufferInfo;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = vk::SharingMode::eExclusive;

	retObject.handle = mContext.getDevice().createBufferUnique(bufferInfo);

	// allocate memory for buffer
	auto memoryReq = mContext.getDevice().getBufferMemoryRequirements(*retObject.handle);

	vk::MemoryAllocateInfo memoryAllocInfo;
	memoryAllocInfo.allocationSize = memoryReq.size;
	memoryAllocInfo.memoryTypeIndex = findMemoryType(memoryReq.memoryTypeBits, memProp, mContext.getPhysicalDevice());

	retObject.memory = mContext.getDevice().allocateMemoryUnique(memoryAllocInfo);
	mContext.getDevice().bindBufferMemory(*retObject.handle, *retObject.memory, 0);

	return retObject;
}

void Utility::copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size, vk::DeviceSize srcOffset, vk::DeviceSize dstOffset)
{
	auto commandBuffer = beginSingleTimeCommands();
	recordCopyBuffer(commandBuffer, src, dst, size, srcOffset, dstOffset);
	endSingleTimeCommands(commandBuffer);
}

ImageParameters Utility::createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling,
	vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memProperties)
{
	vk::ImageCreateInfo imageInfo;
	imageInfo.imageType = vk::ImageType::e2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = vk::ImageLayout::eUndefined;
	imageInfo.usage = usage;
	imageInfo.sharingMode = vk::SharingMode::eExclusive; 
	imageInfo.samples = vk::SampleCountFlagBits::e1;

	auto image = mContext.getDevice().createImageUnique(imageInfo);

	// allocate image memory
	auto memoryReq = mContext.getDevice().getImageMemoryRequirements(*image);

	vk::MemoryAllocateInfo allocInfo;
	allocInfo.allocationSize = memoryReq.size;
	allocInfo.memoryTypeIndex = findMemoryType(memoryReq.memoryTypeBits, memProperties, mContext.getPhysicalDevice());

	auto memory = mContext.getDevice().allocateMemoryUnique(allocInfo);
	mContext.getDevice().bindImageMemory(*image, *memory, 0);

	return ImageParameters{ std::move(image), vk::UniqueImageView(), std::move(memory), format };
}

void Utility::copyImage(vk::Image srcImage, vk::Image dstImage, uint32_t width, uint32_t height)
{
	vk::CommandBuffer commandBuffer = beginSingleTimeCommands();
	recordCopyImage(commandBuffer, srcImage, dstImage, width, height);
	endSingleTimeCommands(commandBuffer);
}

void Utility::transitImageLayout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout)
{
	vk::CommandBuffer commandBuffer = beginSingleTimeCommands();
	recordTransitImageLayout(commandBuffer, image, oldLayout, newLayout);
	endSingleTimeCommands(commandBuffer);
}

vk::UniqueImageView Utility::createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags flags)
{
	vk::ImageViewCreateInfo viewInfo;
	viewInfo.image = image;
	viewInfo.viewType = vk::ImageViewType::e2D;
	viewInfo.format = format;

	// no swizzle
	//viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	//viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	//viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	//viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	viewInfo.subresourceRange.aspectMask = flags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	return mContext.getDevice().createImageViewUnique(viewInfo);
}

ImageParameters Utility::loadImageFromFile(std::string path)
{
	// load image file
	unsigned width, height;
	std::vector<unsigned char> pixels;

	if (lodepng::decode(pixels, width, height, path))
		throw std::runtime_error("Failed to load png file: " + path);

	return loadImageFromMemory(pixels, width, height);
}

ImageParameters Utility::loadImageFromMemory(std::vector<uint8_t> pixels, size_t width, size_t height)
{
	// create staging image memory
	auto stagingBuffer = createBuffer(
		width * height * 4,
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
	);

	// copy image to staging memory
	void* data;
	vkMapMemory(mContext.getDevice(), *stagingBuffer.memory, 0, stagingBuffer.size, 0, &data);
	memcpy(data, pixels.data(), static_cast<uint32_t>(stagingBuffer.size));
	vkUnmapMemory(mContext.getDevice(), *stagingBuffer.memory);

	auto image = createImage(
		width, height,
		vk::Format::eR8G8B8A8Unorm,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	auto cmd = beginSingleTimeCommands();
	recordTransitImageLayout(cmd, *image.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	recordCopyBuffer(cmd, *stagingBuffer.handle, *image.handle, width, height);
	recordTransitImageLayout(cmd, *image.handle, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	endSingleTimeCommands(cmd);

	image.view = createImageView(*image.handle, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);

	return image;
}

vk::CommandBuffer Utility::beginSingleTimeCommands()
{
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandPool = mContext.getStaticCommandPool();
	allocInfo.commandBufferCount = 1;

	auto commandBuffer = mContext.getDevice().allocateCommandBuffers(allocInfo)[0];

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	commandBuffer.begin(beginInfo);

	return commandBuffer;
}

void Utility::endSingleTimeCommands(vk::CommandBuffer buffer)
{
	buffer.end();
	
	// execute the command buffer and wait for the execution
	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &buffer;
	mContext.getGraphicsQueue().submit(submitInfo, nullptr);
	mContext.getGraphicsQueue().waitIdle();

	// free the temperorary command buffer
	mContext.getDevice().freeCommandBuffers(mContext.getStaticCommandPool(), buffer);
}

void Utility::recordCopyBuffer(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::Buffer dst, vk::DeviceSize size,
	vk::DeviceSize srcOffset, vk::DeviceSize dstOffset)
{
	vk::BufferCopy copyRegion;
	copyRegion.srcOffset = srcOffset;
	copyRegion.dstOffset = dstOffset;
	copyRegion.size = size;

	cmdBuffer.copyBuffer(src, dst, copyRegion);
}

void Utility::recordCopyBuffer(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::Image dst, uint32_t width, uint32_t height, vk::DeviceSize srcOffset)
{
	vk::ImageSubresourceLayers subresource;
	subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	subresource.baseArrayLayer = 0;
	subresource.mipLevel = 0;
	subresource.layerCount = 1;

	vk::BufferImageCopy region;
	region.bufferOffset = 0;
	region.imageSubresource = subresource;
	region.imageOffset = vk::Offset3D(0, 0, 0);
	region.imageExtent = vk::Extent3D(width, height, 1);

	cmdBuffer.copyBufferToImage(src, dst, vk::ImageLayout::eTransferDstOptimal, region);
}

void Utility::recordCopyImage(vk::CommandBuffer cmdBuffer, vk::Image src, vk::Image dst, uint32_t width, uint32_t height)
{
	vk::ImageSubresourceLayers subresource;
	subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	subresource.baseArrayLayer = 0;
	subresource.mipLevel = 0;
	subresource.layerCount = 1;

	vk::ImageCopy region;
	region.srcSubresource = subresource;
	region.dstSubresource = subresource;
	region.srcOffset = vk::Offset3D(0, 0, 0);
	region.dstOffset = vk::Offset3D(0, 0, 0);
	region.extent.width = width;
	region.extent.height = height;
	region.extent.depth = 1;

	cmdBuffer.copyImage(src, vk::ImageLayout::eTransferSrcOptimal, dst, vk::ImageLayout::eTransferDstOptimal, region);
}

void Utility::recordTransitImageLayout(vk::CommandBuffer cmdBuffer, vk::Image image, vk::ImageLayout oldLayout,	vk::ImageLayout newLayout)
{
	// barrier is used to ensure a buffer has finished writing before
	// reading as well as doing transition
	vk::ImageMemoryBarrier barrier;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;  //TODO: when converting the depth attachment for depth pre pass this is not correct
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;

	if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal || oldLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
	else
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;

	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
	vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eTopOfPipe;

	//if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferSrcOptimal)
	//{
	//	// dst must wait on src
	//	barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
	//	barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
	//	srcStage = vk::PipelineStageFlagBits::eHost;
	//	dstStage = vk::PipelineStageFlagBits::eTransfer;
	//}
	//else 
	if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
	{
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		dstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
		srcStage = vk::PipelineStageFlagBits::eTransfer;
		dstStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
	{
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead	| vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
	}
	else if (oldLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	}
	else if (oldLayout == vk::ImageLayout::eShaderReadOnlyOptimal && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
	}
	else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	}
	// else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eColorAttachmentOptimal)
	// {
	// 	
	// }
	else
		throw std::runtime_error("Unsupported layout transition");
	
	cmdBuffer.pipelineBarrier(srcStage, dstStage, {}, nullptr, nullptr, barrier);
}
