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

struct ImageParameters
{
	vk::UniqueImage handle;
	vk::UniqueImageView view;
	vk::UniqueDeviceMemory memory;
};

namespace util
{
	vk::VertexInputBindingDescription getVertexBindingDesciption();
	std::array<vk::VertexInputAttributeDescription, 4> getVertexAttributeDescriptions();
	std::vector<uint32_t> compileShader(const std::string& filename);

	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 color;
		glm::vec2 texCoord;
		glm::vec3 normal;

		bool operator==(const Vertex& o) const noexcept
		{
			return pos == o.pos && color == o.color && texCoord == o.texCoord && normal == o.normal;
		}

		size_t hash() const
		{
			size_t seed = 0;
			seed ^= std::hash<glm::vec3>()(pos) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= std::hash<glm::vec3>()(color) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= std::hash<glm::vec2>()(texCoord) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= std::hash<glm::vec3>()(normal) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			return seed;
		}
	};

	inline bool isNearlyEqual(float a, float b, float tolerance = 1e-8f)
	{
		return glm::abs(a - b) <= tolerance;
	}
}

class Utility
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

	ImageParameters loadImageFromFile(std::string path);

	vk::CommandBuffer beginSingleTimeCommands();
	void endSingleTimeCommands(vk::CommandBuffer buffer);

	void recordCopyBuffer(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::Buffer dst, vk::DeviceSize size, vk::DeviceSize srcOffset = 0, vk::DeviceSize dstOffset = 0);
	void recordCopyBuffer(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::Image dst, uint32_t width, uint32_t height, vk::DeviceSize srcOffset = 0);
	void recordCopyImage(vk::CommandBuffer cmdBuffer, vk::Image src, vk::Image dst, uint32_t width, uint32_t height);
	void recordTransitImageLayout(vk::CommandBuffer cmdBuffer, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
private:
	const Context& mContext;
};