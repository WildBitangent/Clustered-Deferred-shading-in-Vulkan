#include "Model.h"
#include "Util.h"

void Model::loadModel(const Context& context, const std::string& path, const vk::Sampler& textureSampler,
	const vk::DescriptorPool& descriptorPool, const vk::DescriptorSetLayout& materialDescriptorSetLayout)
{
	Utility utility(context);
	
	auto buffer = utility.createBuffer(
		4 * sizeof(util::Vertex) + 6 * 4,
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
	);

	mBuffer = std::move(buffer.handle);
	mMemory = std::move(buffer.memory);

	BufferSection vertexSection(*mBuffer, 0, 4 * sizeof(util::Vertex));
	BufferSection indexSection(*mBuffer, 4 * sizeof(util::Vertex), 6 * 4);

	MeshPart part(vertexSection, indexSection, 6);

	auto data = context.getDevice().mapMemory(*mMemory, 0, 4 * sizeof(util::Vertex) + 6 * 4);
	auto vertices = reinterpret_cast<util::Vertex*>(data);
	vertices[0].pos = { -0.5f, -0.5f, 0.0f };
	vertices[0].color = { 1.0f, 0.0f, 0.0f };
	vertices[1].pos = { 0.5f, -0.5f, 0.0f };
	vertices[1].color = { 0.0f, 1.0f, 0.0f };
	vertices[2].pos = { 0.5f, 0.5f, 0.0f };
	vertices[2].color = { 0.0f, 0.0f, 1.0f };
	vertices[3].pos = { -0.5f, 0.5f, 0.0f };
	vertices[3].color = { 0.0f, 1.0f, 1.0f };

	auto indices = reinterpret_cast<uint32_t*>(&vertices[4]);
	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 2;
	indices[4] = 3;
	indices[5] = 0;

	context.getDevice().unmapMemory(*mMemory);

	mParts.emplace_back(part);
}

const std::vector<MeshPart>& Model::getMeshParts() const
{
	return mParts;
}
