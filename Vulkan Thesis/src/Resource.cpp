#include "Resource.h"
#include "Model.h"
#include <iostream>

using namespace resource;

vk::PipelineLayout PipelineLayout::add(const std::string& key, vk::PipelineLayoutCreateInfo& createInfo)
{
	return *mData.insert_or_assign(key, mDevice.createPipelineLayoutUnique(createInfo)).first->second;
}

vk::Pipeline Pipeline::add(const std::string& key, vk::PipelineCache& cache, vk::GraphicsPipelineCreateInfo& createInfo)
{
	return *mData.insert_or_assign(key, mDevice.createGraphicsPipelineUnique(cache, createInfo)).first->second;
}

vk::Pipeline Pipeline::add(const std::string& key, vk::PipelineCache& cache, vk::ComputePipelineCreateInfo& createInfo)
{
	return *mData.insert_or_assign(key, mDevice.createComputePipelineUnique(cache, createInfo)).first->second;
}

vk::DescriptorSetLayout DescriptorSetLayout::add(const std::string& key, vk::DescriptorSetLayoutCreateInfo& createInfo)
{
	return *mData.insert_or_assign(key, mDevice.createDescriptorSetLayoutUnique(createInfo)).first->second;
}

vk::DescriptorSet DescriptorSet::add(const std::string& key, vk::DescriptorSetAllocateInfo allocInfo)
{
	return *mData.insert_or_assign(key, std::move(mDevice.allocateDescriptorSetsUnique(allocInfo)[0])).first->second;
}

vk::ShaderModule ShaderModule::add(const std::string& key)
{
	try // TODO must be done with own exception type
	{
		auto spirv = util::compileShader(key);

		vk::ShaderModuleCreateInfo shaderInfo;
		shaderInfo.codeSize = spirv.size() * sizeof(uint32_t);
		shaderInfo.pCode = spirv.data();

		mData.insert_or_assign(key, mDevice.createShaderModuleUnique(shaderInfo));
	}
	catch (std::runtime_error& err)
	{
		std::cout << err.what() << std::endl;
	}

	return *mData[key];
}

Resources::Resources(const vk::Device device)
	: pipelineLayout(device)
	, pipeline(device)
	, descriptorSetLayout(device)
	, descriptorSet(device)
	, shaderModule(device)
{
}


