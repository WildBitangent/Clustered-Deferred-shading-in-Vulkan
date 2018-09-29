#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>
#include "Context.h"
#include "Util.h"


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

struct MeshPart
{
	BufferSection vertexBufferSection;
	BufferSection indexBufferSection;
	BufferSection materialUniformSection;

	vk::DescriptorSet materialDescriptorSet; // TODO: global material atlas

	vk::ImageView albedoMap; // Should be just references // TODO refactor
	vk::ImageView normalMap;

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
	Model() = default;
	~Model() = default;

	Model(const Model& model) = delete;
	Model& operator=(const Model& model) = delete;
	Model(Model&& model) = default;
	Model& operator=(Model&& model) = default;

	void loadModel(const Context& context, const std::string& path, const vk::Sampler& textureSampler,
		const vk::DescriptorPool& descriptorPool, const vk::DescriptorSetLayout& materialDescriptorSetLayout); // TODO: refactor

	const std::vector<MeshPart>& getMeshParts() const;

private:
	std::vector<MeshPart> mParts;

	BufferParameters mBuffer;
	BufferParameters mUniformBuffer;

	std::vector<ImageParameters> mImages;
};