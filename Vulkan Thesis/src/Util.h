#pragma once
#define GLM_ENABLE_EXPERIMENTAL

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

class Context;

struct BufferParameters
{
	vk::UniqueBuffer handle;
	vk::UniqueDeviceMemory memory;
	size_t size;
};

struct BufferSection
{
	BufferSection() = default;
	BufferSection(vk::Buffer handle, vk::DeviceSize offset, vk::DeviceSize size)
		: handle(handle)
		, offset(offset)
		, size(size)
	{}

	vk::Buffer handle;
	vk::DeviceSize offset = 0;
	vk::DeviceSize size = 0;
};

struct ImageParameters
{
	vk::UniqueImage handle;
	vk::UniqueImageView view;
	vk::UniqueDeviceMemory memory;
	vk::Format format;
};

struct GBuffer
{
	ImageParameters depth;
	ImageParameters position;
	ImageParameters color;
	ImageParameters normal;
};

namespace util
{
	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 color;
		glm::vec2 texCoord;
		glm::vec3 normal;
		glm::vec3 tangent = glm::vec3(0.0f);

		bool operator==(const Vertex& o) const noexcept
		{
			return pos == o.pos && color == o.color && texCoord == o.texCoord && normal == o.normal;
		}

		size_t hash() const;
	};

	namespace init
	{
		// vk::WriteDescriptorSet writeDescriptorSet(vk::DescriptorSet target, )
	}

	vk::VertexInputBindingDescription getVertexBindingDesciption();
	std::array<vk::VertexInputAttributeDescription, 5> getVertexAttributeDescriptions();
	std::vector<uint32_t> compileShader(const std::string& filename);
	vk::WriteDescriptorSet createDescriptorWriteBuffer(vk::DescriptorSet target, uint32_t binding, vk::DescriptorType type, vk::DescriptorBufferInfo& bufferInfo);
	vk::WriteDescriptorSet createDescriptorWriteImage(vk::DescriptorSet target, uint32_t binding, vk::DescriptorImageInfo& imageInfo);
}

class Utility // TODO make it singleton (all functions in namespace using singleton, who owns device context etc.)
{
public:
	Utility(const Context& context);

	vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
	vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availableModes);
	vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);
	vk::Format findSupportedFormat(const std::vector<vk::Format>& formats, vk::ImageTiling tiling, vk::FormatFeatureFlags features);

	BufferParameters createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memProp);
	void copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size, vk::DeviceSize srcOffset = 0, vk::DeviceSize dstOffset = 0);

	ImageParameters createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memProperties);
	void copyImage(vk::Image srcImage, vk::Image dstImage, uint32_t width, uint32_t height);
	void transitImageLayout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
	vk::UniqueImageView createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags flags);

	std::vector<unsigned char> loadImageFromFile(std::string path) const;
	ImageParameters loadImageFromMemory(std::vector<uint8_t> pixels, size_t width, size_t height);

	vk::CommandBuffer beginSingleTimeCommands();
	void endSingleTimeCommands(vk::CommandBuffer buffer);

	void recordCopyBuffer(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::Buffer dst, vk::DeviceSize size, vk::DeviceSize srcOffset = 0, vk::DeviceSize dstOffset = 0);
	void recordCopyBuffer(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::Image dst, uint32_t width, uint32_t height, vk::DeviceSize srcOffset = 0);
	void recordCopyImage(vk::CommandBuffer cmdBuffer, vk::Image src, vk::Image dst, uint32_t width, uint32_t height);
	void recordTransitImageLayout(vk::CommandBuffer cmdBuffer, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
private:
	const Context& mContext;
};
