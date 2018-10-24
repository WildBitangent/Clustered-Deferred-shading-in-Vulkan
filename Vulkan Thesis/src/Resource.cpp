#include "Resource.h"

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

Resources::Resources(const vk::Device device)
	: pipelineLayout(device)
	, pipeline(device)
	, descriptorSetLayout(device)
	, descriptorSet(device)
{
}


