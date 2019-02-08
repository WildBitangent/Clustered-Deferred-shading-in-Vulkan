#include "Model.h"
#include "Util.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <fstream>
#include <experimental/filesystem>

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

	struct MeshMaterialGroup // grouped by material
	{
		std::vector<util::Vertex> vertices;
		std::vector<uint32_t> indices;

		std::string albedoMapPath;
		std::string normalMapPath;
		std::string specularMapPath;
	};

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

void Model::loadModel(const Context& context, const std::string& path, const vk::Sampler& textureSampler,
	const vk::DescriptorPool& descriptorPool, Resources& resources)
{
	auto device = context.getDevice();
	Utility utility{ context };

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

	vk::DeviceSize currentOffset = 0;
	for (const auto& group : groups)
	{
		if (group.indices.size() <= 0)
			continue;

		vk::DeviceSize vertexSectionSize = sizeof(util::Vertex) * group.vertices.size();
		vk::DeviceSize indexSectionSize = sizeof(uint32_t) * group.indices.size();

		// copy vertex data
		BufferSection vertexBufferSection = { *mBuffer.handle, currentOffset, vertexSectionSize };
		{
			BufferParameters stagingBuffer = utility.createBuffer(
				vertexSectionSize,
				vk::BufferUsageFlagBits::eTransferSrc,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
			);

			void* data = device.mapMemory(*stagingBuffer.memory, 0, stagingBuffer.size, {});
			memcpy(data, group.vertices.data(), static_cast<size_t>(stagingBuffer.size));
			device.unmapMemory(*stagingBuffer.memory);

			utility.copyBuffer(*stagingBuffer.handle, *mBuffer.handle, stagingBuffer.size, 0, currentOffset);

			currentOffset += stagingBuffer.size;
		}

		// copy index data
		BufferSection indexBufferSection = { *mBuffer.handle, currentOffset, indexSectionSize };
		{
			BufferParameters stagingBuffer = utility.createBuffer(
				indexSectionSize,
				vk::BufferUsageFlagBits::eTransferSrc,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
			);

			void* data = device.mapMemory(*stagingBuffer.memory, 0, stagingBuffer.size, {});
			memcpy(data, group.indices.data(), stagingBuffer.size);
			device.unmapMemory(*stagingBuffer.memory);

			utility.copyBuffer(*stagingBuffer.handle, *mBuffer.handle, stagingBuffer.size, 0, currentOffset);

			currentOffset += stagingBuffer.size;
		}

		MeshPart part(vertexBufferSection, indexBufferSection, group.indices.size());

		if (!group.albedoMapPath.empty())
		{
			mImages.emplace_back(utility.loadImageFromFile(group.albedoMapPath));
			part.albedoMap = *mImages.back().view;
			part.hasAlbedo = true;
		}
		else
			part.albedoMap = *mImages[0].view;

		if (!group.normalMapPath.empty())
		{
			mImages.emplace_back(utility.loadImageFromFile(group.normalMapPath));
			part.normalMap = *mImages.back().view;
			part.hasNormal = true;
		}
		else
			part.normalMap = *mImages[0].view;

		if (!group.specularMapPath.empty())
		{
			mImages.emplace_back(utility.loadImageFromFile(group.specularMapPath));
			part.specularMap = *mImages.back().view;
			part.hasSpecular = true;
		}
		else
			part.specularMap = *mImages[0].view;

		mParts.emplace_back(part);
	}

	auto createMaterialDescriptorSet = 
		[&utility, &device, &textureSampler, &descriptorPool, &resources, &memory = *mUniformBuffer.memory]
		(MeshPart& part, BufferSection uniformBufferSection)
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &resources.descriptorSetLayout.get("material");

		auto targetSet = resources.descriptorSet.add(part.materialDescriptorSetKey, allocInfo);
		MaterialUBO ubo;

		std::vector<vk::WriteDescriptorSet> descriptorWrites;

		// refer to the uniform object buffer
		vk::DescriptorBufferInfo uniformBufferInfo;
		{
			uniformBufferInfo.buffer = uniformBufferSection.handle;
			uniformBufferInfo.offset = uniformBufferSection.offset;
			uniformBufferInfo.range = uniformBufferSection.size;

			vk::WriteDescriptorSet writeDescriptor;
			writeDescriptor.dstSet = targetSet;
			writeDescriptor.dstBinding = 0;
			writeDescriptor.descriptorCount = 1;
			writeDescriptor.descriptorType = vk::DescriptorType::eUniformBuffer;
			writeDescriptor.pBufferInfo = &uniformBufferInfo;

			descriptorWrites.emplace_back(writeDescriptor);
		}

		vk::DescriptorImageInfo albedoMapInfo;
		
		{
			ubo.hasAlbedoMap = part.hasAlbedo;
			albedoMapInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			albedoMapInfo.imageView = part.albedoMap;
			albedoMapInfo.sampler = textureSampler;

			vk::WriteDescriptorSet writeDescriptor;
			writeDescriptor.dstSet = targetSet;
			writeDescriptor.dstBinding = 1;
			writeDescriptor.descriptorCount = 1;
			writeDescriptor.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			writeDescriptor.pImageInfo = &albedoMapInfo;

			descriptorWrites.emplace_back(writeDescriptor);
		}

		vk::DescriptorImageInfo normalmapInfo;
		{
			ubo.hasNormalMap = part.hasNormal;
			normalmapInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			normalmapInfo.imageView = part.normalMap;
			normalmapInfo.sampler = textureSampler;

			vk::WriteDescriptorSet writeDescriptor;
			writeDescriptor.dstSet = targetSet;
			writeDescriptor.dstBinding = 2;
			writeDescriptor.descriptorCount = 1;
			writeDescriptor.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			writeDescriptor.pImageInfo = &normalmapInfo;

			descriptorWrites.emplace_back(writeDescriptor);
		}

		vk::DescriptorImageInfo specularmapInfo;
		{
			ubo.hasSpecularMap = part.hasSpecular;
			specularmapInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			specularmapInfo.imageView = part.specularMap;
			specularmapInfo.sampler = textureSampler;

			vk::WriteDescriptorSet writeDescriptor;
			writeDescriptor.dstSet = targetSet;
			writeDescriptor.dstBinding = 3;
			writeDescriptor.descriptorCount = 1;
			writeDescriptor.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			writeDescriptor.pImageInfo = &specularmapInfo;

			descriptorWrites.emplace_back(writeDescriptor);
		}

		device.updateDescriptorSets(descriptorWrites, {});
		
		BufferParameters stagingBuffer = utility.createBuffer(
			uniformBufferSection.size,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);

		void* data = device.mapMemory(*stagingBuffer.memory, 0, stagingBuffer.size, {});
		memcpy(data, &ubo, stagingBuffer.size);
		device.unmapMemory(*stagingBuffer.memory);

		utility.copyBuffer(*stagingBuffer.handle, uniformBufferInfo.buffer, stagingBuffer.size, 0, uniformBufferInfo.offset);
	};


	auto minAlignment = context.getPhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment;
	vk::DeviceSize alignmentOffset = ((sizeof(MaterialUBO) - 1) / minAlignment + 1) * minAlignment;

	mUniformBuffer = utility.createBuffer(
		alignmentOffset * mParts.size(),
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eUniformBuffer,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	vk::DeviceSize totalOffset = 0;
	size_t counter = 0;
	for (auto& part : mParts)
	{
		part.materialDescriptorSetKey += std::to_string(counter++); // TODO sigh, refactor this shit
		createMaterialDescriptorSet(part, { *mUniformBuffer.handle, totalOffset, sizeof(MaterialUBO) });
		totalOffset += alignmentOffset;
	}
}

const std::vector<MeshPart>& Model::getMeshParts() const
{
	return mParts;
}
