#include "Model.h"
#include "Util.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <fstream>
#include <experimental/filesystem>
#include <thread>
#include "lodepng.h"
#include <iostream>

using namespace resource;

namespace std 
{
	template<> 
	struct hash<util::Vertex>
	{
		size_t operator()(util::Vertex const& vertex) const
		{
			return vertex.hash();
		}
	};
}

namespace 
{
	namespace fs = std::experimental::filesystem;

	struct MaterialUBO
	{
		uint32_t hasAlbedoMap = 0;
		uint32_t hasNormalMap = 0;
		uint32_t hasSpecularMap = 0;
	};

	template<typename... T>
	void write4B(std::ofstream& file, const uint32_t& head, const T&... tail)
	{
		file.write(reinterpret_cast<const char*>(&head), sizeof(head));
		
		if constexpr (sizeof...(tail) > 0)
			write4B(file, tail...);
	}

	template<typename... T>
	void read4B(std::ifstream& file, uint32_t& head, T&... tail)
	{
		file.read(reinterpret_cast<char*>(&head), sizeof(head));

		if constexpr (sizeof...(tail) > 0)
			read4B(file, tail...);
	}
	
	void writeCacheModelData(const std::vector<MeshMaterialGroup>& group, const fs::path& path)
	{
		auto folder = path.parent_path();
		auto filename = path.stem().concat(".asd");

		std::ofstream file(folder / filename, std::ios::binary);

		auto write = [&file](const auto* ptr, size_t size)
		{
			file.write(reinterpret_cast<const char*>(ptr), size);
		};

		write4B(file, 0, group.size());
		for (const auto& m : group)
		{
			write4B(file, m.indices.size(), m.vertices.size(), m.albedoMapPath.size(), m.normalMapPath.size(), m.specularMapPath.size());
			write(m.albedoMapPath.data(), m.albedoMapPath.size());
			write(m.normalMapPath.data(), m.normalMapPath.size());
			write(m.specularMapPath.data(), m.specularMapPath.size());
			write(m.indices.data(), m.indices.size() * sizeof(uint32_t));
			write(m.vertices.data(), m.vertices.size() * sizeof(util::Vertex));
		}
	}

	std::vector<MeshMaterialGroup> readCacheModelData(std::ifstream& file)
	{

		uint32_t header, groupSize;
		read4B(file, header, groupSize);

		std::vector<MeshMaterialGroup> materialGroups(groupSize);
		for (auto& m : materialGroups)
		{
			uint32_t indexCount, vertexCount, albedoPathSize, normalPathSize, specularPathSize;
			read4B(file, indexCount, vertexCount, albedoPathSize, normalPathSize, specularPathSize);

			m.albedoMapPath.resize(albedoPathSize);
			file.read(reinterpret_cast<char*>(m.albedoMapPath.data()), albedoPathSize);
			
			m.normalMapPath.resize(normalPathSize);
			file.read(reinterpret_cast<char*>(m.normalMapPath.data()), normalPathSize);

			m.specularMapPath.resize(specularPathSize);
			file.read(reinterpret_cast<char*>(m.specularMapPath.data()), specularPathSize);

			m.indices.resize(indexCount);
			file.read(reinterpret_cast<char*>(m.indices.data()), indexCount * sizeof(uint32_t));

			m.vertices.resize(vertexCount);
			file.read(reinterpret_cast<char*>(m.vertices.data()), vertexCount * sizeof(util::Vertex));
		}

		return materialGroups;
	}

	std::vector<MeshMaterialGroup> loadModelFromFile(const std::string& path)
	{
		auto folder = fs::path(path).parent_path();
		
		if (std::ifstream cacheFile((folder / fs::path(path).stem()).concat(".asd"), std::ios::binary); cacheFile.is_open())
			return readCacheModelData(cacheFile);

		// Cache file not found //TODO header into cache
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;

		if (std::string err; !tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.c_str(), folder.string().c_str()))
			throw std::runtime_error(err);

		std::vector<MeshMaterialGroup> materialGroups(materials.size() + 1); // group parts of the same material together, +1 for unknown material

		for (size_t i = 0; i < materials.size(); i++)
		{
			if (!materials[i].diffuse_texname.empty())
				materialGroups[i + 1].albedoMapPath = (folder / materials[i].diffuse_texname).string();
			if (!materials[i].normal_texname.empty())
				materialGroups[i + 1].normalMapPath = (folder / materials[i].normal_texname).string();
			if (!materials[i].specular_texname.empty())
				materialGroups[i + 1].specularMapPath = (folder / materials[i].specular_texname).string();
			//else if (!materials[i].bump_texname.empty())
			//{
			//	// CryEngine sponza scene uses keyword "bump" to store normal
			//	groups[i + 1].normalMapPath = folder + materials[i].bump_texname;
			//}
		}



		std::vector<std::vector<util::Vertex>> verticesMap(materials.size() + 1);

		for (const auto& shape : shapes)
		{
			size_t indexOffset = 0;

			for (size_t n = 0; n < shape.mesh.num_face_vertices.size(); n++)
			{
				// 0 for undefinde material (tinyObj loader uses -1 for undefined)
				auto& vertices = verticesMap[shape.mesh.material_ids[n] + 1]; 
				auto ngon = shape.mesh.num_face_vertices[n];

				for (size_t f = 0; f < ngon; f++)
				{
					const auto& index = shape.mesh.indices[indexOffset + f];
					util::Vertex vertex;

					vertex.pos = 
					{
						attrib.vertices[3 * index.vertex_index + 0],
						attrib.vertices[3 * index.vertex_index + 1],
						attrib.vertices[3 * index.vertex_index + 2]
					};

					vertex.color =
					{
						attrib.colors[3 * index.vertex_index + 0],
						attrib.colors[3 * index.vertex_index + 1],
						attrib.colors[3 * index.vertex_index + 2]
					};
					
					vertex.texCoord = 
					{
						attrib.texcoords[2 * index.texcoord_index + 0],
						1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
					};

					vertex.normal = 
					{
						attrib.normals[3 * index.normal_index + 0],
						attrib.normals[3 * index.normal_index + 1],
						attrib.normals[3 * index.normal_index + 2]
					};
					
					vertices.emplace_back(vertex);
				}

				// count tangent vector
				glm::vec3 tangent(0.0f);
				auto edge1 = vertices[1].pos - vertices[0].pos;
				auto edge2 = vertices[2].pos - vertices[0].pos;
				auto dtUV1 = vertices[1].texCoord - vertices[0].texCoord;
				auto dtUV2 = vertices[2].texCoord - vertices[0].texCoord;

				float f = 1.0f / (dtUV1.x * dtUV2.y - dtUV2.x * dtUV1.y);

				tangent.x = f * (dtUV2.y * edge1.x - dtUV1.y * edge2.x);
				tangent.y = f * (dtUV2.y * edge1.y - dtUV1.y * edge2.y);
				tangent.z = f * (dtUV2.y * edge1.z - dtUV1.y * edge2.z);
				tangent = glm::normalize(tangent);

				for (size_t i = vertices.size() - ngon; i < vertices.size(); i++)
					vertices[i].tangent += tangent;

				indexOffset += ngon;
			}
		}

		std::vector<std::unordered_map<util::Vertex, uint32_t>> uniqueVerticesPerMaterialGroup(materials.size() + 1);
		
		for (size_t materialID = 0; materialID < verticesMap.size(); materialID++)
		{
			auto& uniqueVertices = uniqueVerticesPerMaterialGroup[materialID];
			auto& group = materialGroups[materialID];

			for (auto& vertex : verticesMap[materialID])
			{
				if (uniqueVertices.count(vertex) == 0)
				{
					vertex.tangent = glm::normalize(vertex.tangent);
					uniqueVertices[vertex] = group.vertices.size(); // auto incrementing size
					group.vertices.push_back(vertex);
				}
				group.indices.emplace_back(uniqueVertices[vertex]);
			}
		}

		writeCacheModelData(materialGroups, path);
		
		return materialGroups;
	}
}

void Model::loadModel(Context& context, const std::string& path, const vk::Sampler& textureSampler,
	const vk::DescriptorPool& descriptorPool, Resources& resources, ThreadPool& pool)
{
	auto device = context.getDevice();
	Utility utility(context);

	// load proxy texture
	mImages.emplace_back(utility.loadImageFromMemory({ 0, 0, 0, 0 }, 1, 1)); 

	auto groups = loadModelFromFile(path);

	vk::DeviceSize bufferSize = 0;
	for (const auto& group : groups)
	{
		if (group.indices.size() <= 0)
			continue;

		bufferSize += sizeof(util::Vertex) * group.vertices.size();
		bufferSize += sizeof(uint32_t) * group.indices.size();
	}

	mBuffer = utility.createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	WorkerStruct work(utility);
	work.groups = std::move(groups);
	work.stagingBuffer = utility.createBuffer( // TODO mb smaller with multiple submits
		256 * 1024 * 1024, // 256 MB
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
	);
	work.data = static_cast<uint8_t*>(device.mapMemory(*work.stagingBuffer.memory, 0, VK_WHOLE_SIZE, {}));

	// create command pools
	vk::CommandPoolCreateInfo poolInfo;
	poolInfo.queueFamilyIndex = context.getQueueFamilyIndices().graphicsFamily.first;
	poolInfo.flags |= vk::CommandPoolCreateFlagBits::eTransient;

	std::vector<vk::UniqueCommandPool> commandPools;
	for (size_t i = 0; i < std::thread::hardware_concurrency(); i++)
		commandPools.emplace_back(device.createCommandPoolUnique(poolInfo));
	
	// create cmd buffers
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.level = vk::CommandBufferLevel::eSecondary;
	allocInfo.commandBufferCount = 1;
	
	for (auto& pool : commandPools)
	{
		allocInfo.commandPool = *pool;
		work.commandBuffers.emplace_back(std::move(device.allocateCommandBuffersUnique(allocInfo)[0]));
	}
	
	allocInfo.commandPool = context.getDynamicCommandPool();
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	vk::UniqueCommandBuffer cmd = std::move(device.allocateCommandBuffersUnique(allocInfo)[0]);

	// spawn workers and wait for finish
	pool.addWorkMultiplex([this, &work](size_t id) { threadWork(id, work); });
	pool.wait();

	// retrieve command buffers
	std::vector<vk::CommandBuffer> cmdBuffers;
	for (auto& cmd : work.commandBuffers)
		cmdBuffers.emplace_back(*cmd);

	// execute all cmd buffers
	cmd->begin(vk::CommandBufferBeginInfo());
	cmd->executeCommands(cmdBuffers);

	// decide min alignment for unform buffers
	auto minAlignment = context.getPhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment;
	vk::DeviceSize alignmentOffset = ((sizeof(MaterialUBO) - 1) / minAlignment + 1) * minAlignment;

	// create uniforms buffer
	mUniformBuffer = utility.createBuffer(
		alignmentOffset * mParts.size(),
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eUniformBuffer,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	// create and update descriptor sets
	vk::DeviceSize currentOffset = work.stagingBufferOffset;
	vk::DeviceSize uniformStartOffset = currentOffset;
	std::vector<vk::WriteDescriptorSet> descriptorWrites;
	std::vector<vk::DescriptorBufferInfo> bufferInfos(mParts.size());
	std::vector<vk::DescriptorImageInfo> imageInfos(mParts.size() * 3);
	size_t counter = 0;

	for (size_t i = 0; i < mParts.size(); i++)
	{
		auto& part = mParts[i];

		part.materialDescriptorSetKey += std::to_string(counter++); // TODO sigh, refactor this shit
		
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &resources.descriptorSetLayout.get("material");

		const auto targetSet = resources.descriptorSet.add(part.materialDescriptorSetKey, allocInfo);
		MaterialUBO ubo;

		bufferInfos[i] = vk::DescriptorBufferInfo(*mUniformBuffer.handle, currentOffset - uniformStartOffset, alignmentOffset);
		imageInfos[i * 3] = vk::DescriptorImageInfo(textureSampler, part.albedoMap, vk::ImageLayout::eShaderReadOnlyOptimal); 
		imageInfos[i * 3 + 1] = vk::DescriptorImageInfo(textureSampler, part.normalMap, vk::ImageLayout::eShaderReadOnlyOptimal); 
		imageInfos[i * 3 + 2] = vk::DescriptorImageInfo(textureSampler, part.specularMap, vk::ImageLayout::eShaderReadOnlyOptimal); 

		descriptorWrites.emplace_back(util::createDescriptorWriteBuffer(targetSet, 0, vk::DescriptorType::eUniformBuffer, bufferInfos[i]));
		descriptorWrites.emplace_back(util::createDescriptorWriteImage(targetSet, 1, imageInfos[i * 3]));
		descriptorWrites.emplace_back(util::createDescriptorWriteImage(targetSet, 2, imageInfos[i * 3 + 1]));
		descriptorWrites.emplace_back(util::createDescriptorWriteImage(targetSet, 3, imageInfos[i * 3 + 2]));

		ubo.hasAlbedoMap = part.hasAlbedo;
		ubo.hasNormalMap = part.hasNormal;
		ubo.hasSpecularMap = part.hasSpecular;

		memcpy(work.data + currentOffset, &ubo, sizeof(MaterialUBO));
		currentOffset += alignmentOffset;
	}

	utility.recordCopyBuffer(
		*cmd, 
		*work.stagingBuffer.handle,
		*mUniformBuffer.handle,
		alignmentOffset * mParts.size(),
		uniformStartOffset
	);

	cmd->end();

	device.unmapMemory(*work.stagingBuffer.memory);

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &*cmd;

	context.getGraphicsQueue().submit(submitInfo, nullptr);
	context.getGraphicsQueue().waitIdle(); // todo mb use fences or semaphores? +1

	device.updateDescriptorSets(descriptorWrites, {});

	work.commandBuffers.clear();
}

void Model::threadWork(size_t threadID, WorkerStruct& work)
{
	auto cmd = *work.commandBuffers[threadID];
	// begin cmd buffer
	vk::CommandBufferInheritanceInfo inheritanceInfo;

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.pInheritanceInfo = &inheritanceInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	cmd.begin(beginInfo);

	while (true) // break condition below
	{
		MeshMaterialGroup group;

		// get work item
		{
			std::lock_guard<std::mutex> lock(work.mutex);

			if (!work.groups.empty())
			{
				group = std::move(work.groups.back());
				work.groups.pop_back();
			}
			else // nothing to be done, finish
				break;
		}
		
		if (group.indices.empty())
			continue;

		vk::DeviceSize vertexSectionSize = sizeof(util::Vertex) * group.vertices.size();
		vk::DeviceSize indexSectionSize = sizeof(uint32_t) * group.indices.size();

		vk::DeviceSize stagingOffset = std::atomic_fetch_add(&work.stagingBufferOffset, vertexSectionSize + indexSectionSize);
		vk::DeviceSize VIOffset = std::atomic_fetch_add(&work.VIBufferOffset, vertexSectionSize + indexSectionSize);

		// copy vertex data
		BufferSection vertexBufferSection = { *mBuffer.handle, VIOffset, vertexSectionSize };
		{
			memcpy(work.data + stagingOffset, group.vertices.data(), static_cast<size_t>(vertexSectionSize));

			work.utility.recordCopyBuffer(
				cmd, 
				*work.stagingBuffer.handle,
				*mBuffer.handle,
				vertexSectionSize,
				stagingOffset,
				VIOffset
			);

			stagingOffset += vertexSectionSize;
			VIOffset += vertexSectionSize;
		}

		// copy index data
		BufferSection indexBufferSection = { *mBuffer.handle, VIOffset, indexSectionSize };
		{
			memcpy(work.data + stagingOffset, group.indices.data(), indexSectionSize);
			
			work.utility.recordCopyBuffer(
				cmd,
				*work.stagingBuffer.handle,
				*mBuffer.handle,
				indexSectionSize,
				stagingOffset,
				VIOffset
			);
		}

		MeshPart part(vertexBufferSection, indexBufferSection, group.indices.size());

		auto loadImg = [&work, &cmd](const std::string& path)
		{
			// load img from file
			unsigned width, height;
			std::vector<unsigned char> pixels;

			if (lodepng::decode(pixels, width, height, path))
				throw std::runtime_error("Failed to load png file: " + path);

			// copy it to the staging buffer // todo copy less
			const auto startOffset = std::atomic_fetch_add(&work.stagingBufferOffset, pixels.size()); 
			memcpy(work.data + startOffset, pixels.data(), pixels.size());

			auto image = work.utility.createImage(
				width, height,
				vk::Format::eR8G8B8A8Unorm,
				vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
				vk::MemoryPropertyFlagBits::eDeviceLocal
			);

			work.utility.recordTransitImageLayout(cmd, *image.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
			work.utility.recordCopyBuffer(cmd, *work.stagingBuffer.handle, *image.handle, width, height, startOffset);
			work.utility.recordTransitImageLayout(cmd, *image.handle, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
			
			image.view = work.utility.createImageView(*image.handle, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);

			return image;
		};

		if (!group.albedoMapPath.empty())
		{
			auto image = loadImg(group.albedoMapPath);

			// lock for insterting to the mImages
			std::lock_guard<std::mutex> lock(work.mutex);
			
			part.albedoMap = *image.view;
			part.hasAlbedo = true;
			mImages.emplace_back(std::move(image));
		}
		else
			part.albedoMap = *mImages[0].view; // todo possibly sigseg on realloc?

		if (!group.normalMapPath.empty())
		{
			auto image = loadImg(group.normalMapPath);

			// lock for insterting to the mImages
			std::lock_guard<std::mutex> lock(work.mutex);
			
			part.normalMap = *image.view;
			part.hasNormal = true;
			mImages.emplace_back(std::move(image));
		}
		else
			part.normalMap = *mImages[0].view;

		if (!group.specularMapPath.empty())
		{
			auto image = loadImg(group.specularMapPath);

			// lock for insterting to the mImages
			std::lock_guard<std::mutex> lock(work.mutex);
			
			part.specularMap = *image.view;
			part.hasSpecular = true;
			mImages.emplace_back(std::move(image));
		}
		else
			part.specularMap = *mImages[0].view;
		
		std::lock_guard<std::mutex> lock(work.mutex);
		mParts.emplace_back(part);
	}

	cmd.end();
}

const std::vector<MeshPart>& Model::getMeshParts() const
{
	return mParts;
}
