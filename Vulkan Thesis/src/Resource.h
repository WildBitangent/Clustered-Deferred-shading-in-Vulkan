#pragma once
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace resource
{
	template<typename U, typename T>
	class Base
	{
	public:
		explicit Base(const vk::Device device) : mDevice(device) {}

		// virtual void add(const std::string& key, const Info& a) = 0;

		// T& get(const std::string& key)
		// {
		// 	return mData.at(key);
		// }

		const T& get(const std::string& key) const
		{
			return *mData.at(key);
		}

	protected:
		std::unordered_map<std::string, U> mData;
		vk::Device mDevice;
	};

	class PipelineLayout : public Base<vk::UniquePipelineLayout, vk::PipelineLayout>
	{
	public:
		explicit PipelineLayout(const vk::Device device) : Base(device) {}

		vk::PipelineLayout add(const std::string& key, vk::PipelineLayoutCreateInfo& createInfo);
	};
	
	class Pipeline : public Base<vk::UniquePipeline, vk::Pipeline>
	{
	public:
		explicit Pipeline(const vk::Device device) : Base(device) {};

		vk::Pipeline add(const std::string& key, vk::PipelineCache& cache, vk::GraphicsPipelineCreateInfo& createInfo);
		vk::Pipeline add(const std::string& key, vk::PipelineCache& cache, vk::ComputePipelineCreateInfo& createInfo);
	};

	class DescriptorSetLayout : public Base<vk::UniqueDescriptorSetLayout, vk::DescriptorSetLayout>
	{
	public:
		explicit DescriptorSetLayout(const vk::Device device) : Base(device) {}

		vk::DescriptorSetLayout add(const std::string& key, vk::DescriptorSetLayoutCreateInfo& createInfo);
	};

	class DescriptorSet : public Base<vk::UniqueDescriptorSet, vk::DescriptorSet>
	{
	public:
		explicit DescriptorSet(const vk::Device device) : Base(device) {}
		
		vk::DescriptorSet add(const std::string& key, vk::DescriptorSetAllocateInfo allocInfo);
	};

	class ShaderModule : public Base<vk::UniqueShaderModule, vk::ShaderModule>
	{
	public:
		explicit ShaderModule(const vk::Device device) : Base(device) {}

		vk::ShaderModule add(const std::string& key);
	};

	struct Resources
	{
		explicit Resources(const vk::Device device);

		PipelineLayout pipelineLayout;
		Pipeline pipeline;
		DescriptorSetLayout descriptorSetLayout;
		DescriptorSet descriptorSet;
		ShaderModule shaderModule;
	};
}




