#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

#include "Context.h"
#include "Util.h"
#include "Resource.h"
#include <queue>
#include <mutex>
#include <atomic>

struct MeshPart
{
	BufferSection vertexBufferSection;
	BufferSection indexBufferSection;
	// BufferSection materialUniformSection;

	std::string materialDescriptorSetKey = "material.";

	vk::ImageView albedoMap; // Should be just references // TODO refactor
	vk::ImageView normalMap;
	vk::ImageView specularMap;

	bool hasAlbedo = false;
	bool hasNormal = false;
	bool hasSpecular = false;

	uint32_t indexCount = 0;

	MeshPart(const BufferSection& vertex, const BufferSection& index, uint32_t indexCount)
		: vertexBufferSection(vertex)
		, indexBufferSection(index)
		, indexCount(indexCount)
	{}
};

struct MeshMaterialGroup // grouped by material
{
	std::vector<util::Vertex> vertices;
	std::vector<uint32_t> indices;

	std::string albedoMapPath;
	std::string normalMapPath;
	std::string specularMapPath;
};

struct WorkerStruct
{
	WorkerStruct(Utility& utility) : utility(utility) {}
	std::vector<MeshMaterialGroup> groups;
	std::vector<vk::UniqueCommandBuffer> commandBuffers;

	Utility& utility;
	uint8_t* data;

	BufferParameters stagingBuffer;
	std::mutex mutex;
	std::atomic<vk::DeviceSize> stagingBufferOffset = 0;
	std::atomic<vk::DeviceSize> VIBufferOffset = 0;
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

	void loadModel(Context& context, const std::string& path, const vk::Sampler& textureSampler,
		const vk::DescriptorPool& descriptorPool, resource::Resources& resources, ThreadPool& pool); // TODO: refactor

	const std::vector<MeshPart>& getMeshParts() const;

private:
	void threadWork(size_t threadID, WorkerStruct& work);
	
private:
	std::vector<MeshPart> mParts;

	BufferParameters mBuffer;
	BufferParameters mUniformBuffer;

	std::vector<ImageParameters> mImages;
};