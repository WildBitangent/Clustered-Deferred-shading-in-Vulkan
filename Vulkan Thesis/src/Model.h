#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>
#include "Context.h"

struct BufferSection
{
	BufferSection() = default;
	BufferSection(vk::Buffer handle, vk::DeviceSize offset, vk::DeviceSize size)
		: handle(handle)
		, offset(offset)
		, size(size)
	{}

	vk::Buffer handle = {};
	vk::DeviceSize offset = 0;
	vk::DeviceSize size = 0;
};

struct MeshPart
{
	BufferSection vertexBufferSection;
	BufferSection indexBufferSection;
	BufferSection materialUniformSection;

	vk::DescriptorSet materialDescriptorSet; // TODO: global material atlas

	uint32_t indexCount = 0;

	MeshPart(const BufferSection& vertex, const BufferSection& index, uint32_t indexCount)
		: vertexBufferSection(vertex)
		, indexBufferSection(index)
		, indexCount(indexCount)
	{}
};

class Model
{
public:

	void loadModel(const Context& context, const std::string& path, const vk::Sampler& textureSampler, 
		const vk::DescriptorPool& descriptorPool, const vk::DescriptorSetLayout& materialDescriptorSetLayout); // TODO: refactor

	const std::vector<MeshPart>& getMeshParts() const;


private:
	std::vector<MeshPart> mParts;

	vk::UniqueBuffer		mBuffer;
	vk::UniqueDeviceMemory	mMemory;

	std::vector<vk::UniqueImage> mImages;
	std::vector<vk::UniqueImageView> mImageviews;
	std::vector<vk::UniqueDeviceMemory> mImageMemories; //TODO: use a single memory, or two
};