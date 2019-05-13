/**
 * @file 'Context.cpp'
 * @brief Graphic context holder
 * @copyright The MIT license 
 * @author Matej Karas
 */

#include "Context.h"

#include <GLFW/glfw3.h>
#include <unordered_set>
#include <iostream>
#include <vulkan/vulkan.hpp>

#ifndef DEBUG
#define ENABLE_VALIDATION_LAYERS
#endif


namespace
{
	const std::vector<const char*> VALIDATION_LAYERS = 
	{
		"VK_LAYER_LUNARG_standard_validation",
	};

	const std::vector<const char*> DEVICE_EXTENSIONS = 
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	bool checkDeviceExtensionSupport(vk::PhysicalDevice device)
	{
		std::unordered_set<std::string> requiredExtensions(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());

		for (const auto& extension : device.enumerateDeviceExtensionProperties())
			requiredExtensions.erase(extension.extensionName);

		return requiredExtensions.empty();
	}

	bool isDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR windowSurface)
	{
		QueueFamilyIndices indices = QueueFamilyIndices::findQueueFamilies(device, windowSurface);

		bool extensionsSupported = checkDeviceExtensionSupport(device);

		auto formats = device.getSurfaceFormatsKHR(windowSurface);
		auto presentModes = device.getSurfacePresentModesKHR(windowSurface);

		return indices.isComplete() && extensionsSupported && (!formats.empty() && !presentModes.empty());
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT msgType,
		const VkDebugUtilsMessengerCallbackDataEXT* callback,
		void* userData)
	{
		vk::DebugUtilsMessageSeverityFlagsEXT messageSeverity(severity);
		vk::DebugUtilsMessageTypeFlagsEXT messageType(msgType);
		vk::DebugUtilsMessengerCallbackDataEXT callbackData(*callback);

		if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose)
			std::cerr << "VERBOSE: ";
		else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo)
			std::cerr << "INFO: ";
		else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
			std::cerr << "WARNING: ";
		else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
			std::cerr << "ERROR: ";

		if (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral)
			std::cerr << "GENERAL";
		else
		{
			if (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation)
				std::cerr << "VALIDATION";
			if (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
			{
				if (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation)
					std::cerr << " | ";

				std::cerr << "PERFORMANCE";
			}
		}

		{
			auto name = (callbackData.pMessageIdName) ? callbackData.pMessageIdName : "";
			std::cerr <<
				" - Message ID Number " << callbackData.messageIdNumber <<
				", Message ID Name: " << name <<
				"\n\t" << callbackData.pMessage;
		}


		if (callbackData.objectCount > 0)
		{
			std::cerr << "\n\n\tObjects - " << callbackData.objectCount << "\n";

			for (size_t object = 0; object < callbackData.objectCount; object++)
			{
				auto name = (callbackData.pObjects[object].pObjectName) ? callbackData.pObjects[object].pObjectName : "";

				std::cerr <<
					"\t\tObject[" << object <<
					"] - Type " << vk::to_string(callbackData.pObjects[object].objectType) <<
					", Handle " << std::hex << std::showbase << callbackData.pObjects[object].objectHandle << std::dec <<
					", Name \"" << name << "\"\n";
			}
		}

		if (callbackData.cmdBufLabelCount > 0)
		{
			std::cerr << "\n\tCommand Buffer Labels - " << callbackData.cmdBufLabelCount << "\n";

			for (size_t label = 0; label < callbackData.cmdBufLabelCount; label++)
			{
				std::cerr <<
					"\t\tLabel[" << label <<
					"] - " << callbackData.pCmdBufLabels[label].pLabelName <<
					" { " << callbackData.pCmdBufLabels[label].color[0] <<
					", " << callbackData.pCmdBufLabels[label].color[1] <<
					", " << callbackData.pCmdBufLabels[label].color[2] <<
					", " << callbackData.pCmdBufLabels[label].color[3] <<
					" }\n\n";
			}
		}

		std::cerr << std::endl;
		return VK_FALSE;
	}
}


bool QueueFamilyIndices::isComplete() const
{
	return generalFamily >= 0 && computeFamily >= 0;
}

bool QueueFamilyIndices::isSingleQueue() const 
{
	return generalFamily == computeFamily;
}

QueueFamilyIndices QueueFamilyIndices::findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface)
{
	QueueFamilyIndices indices;

	auto queueFamilies = device.getQueueFamilyProperties();

	// try to find queue for compute, graphics and present - standard says that there should be 1 universal queue on every device
	auto flag = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
	for (int i = 0; i < static_cast<int>(queueFamilies.size()) && indices.generalFamily < 0; i++)
		if (queueFamilies[i].queueFlags & flag && device.getSurfaceSupportKHR(static_cast<uint32_t>(i), surface))
			indices.generalFamily = i;

	// try to pick async
	for (int i = 0; i < static_cast<int>(queueFamilies.size()); i++)
	{
		if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute)
		{
			const auto lastQueue = indices.computeFamily;
			indices.computeFamily = i;
			
			if (i != indices.generalFamily)
			{
				indices.computeQueueIndex = 0;
				break;
			}
				
			if (queueFamilies[i].queueCount > 1)
				indices.computeQueueIndex = 1;
			else if (indices.computeQueueIndex == 1) // try to pick another queue in family, which could be async too
				indices.computeFamily = lastQueue;
		}
	}

	if (!indices.isComplete())
		throw std::runtime_error("Failed to pick appropriate queue families");

	return indices;
}

Context::Context(GLFWwindow* window)
	: mWindow(window)
{
	if (!window)
		throw std::runtime_error("Invalid window");

	createInstance();
	setupDebugCallback();
	createWindowSurface();
	pickPhysicalDevice();
	findQueueFamilyIndices();
	createLogicalDevice();
	createCommandPools();
}

void Context::createInstance()
{
#ifdef ENABLE_VALIDATION_LAYERS
	auto availableLayers = vk::enumerateInstanceLayerProperties();

	for (const char* layerName : VALIDATION_LAYERS)
	{
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers)
		{
			if (strcmp(layerName, layerProperties.layerName) == 0)
			{
				layerFound = true;
				break;
			}
		}

		if (!layerFound)
			throw std::runtime_error("Validation layer not found");
	}
#endif

	vk::ApplicationInfo appInfo;
	appInfo.pApplicationName = "Vulkan Hello World";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_1;

	vk::InstanceCreateInfo instanceInfo;
	instanceInfo.pApplicationInfo = &appInfo;

	// Getting Vulkan instance extensions required by GLFW
	std::vector<const char*> extensions;

	unsigned int glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	for (unsigned int i = 0; i < glfwExtensionCount; i++)
		extensions.push_back(glfwExtensions[i]);

#ifdef ENABLE_VALIDATION_LAYERS 
	extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

	// Getting Vulkan supported extensions
	std::unordered_set<std::string> supportedExtensionNames;
	for (const auto& extension : vk::enumerateInstanceExtensionProperties())
		supportedExtensionNames.insert(std::string(extension.extensionName));

	// Check for and print any unsupported extension
	for (const auto& eName : extensions)
		if (supportedExtensionNames.count(eName) <= 0)
			std::cerr << "Unsupported extension required by GLFW: " << eName << std::endl;
	
	// Enable required extensions
	instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	instanceInfo.ppEnabledExtensionNames = extensions.data();

#ifdef ENABLE_VALIDATION_LAYERS
	instanceInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
	instanceInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
#endif

	mInstance = createInstanceUnique(instanceInfo);
}

void Context::setupDebugCallback()
{
#ifdef ENABLE_VALIDATION_LAYERS
	vk::DebugUtilsMessengerCreateInfoEXT createInfo;
	createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
	createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
	createInfo.pfnUserCallback = debugCallback;

	static auto dldi = vk::DispatchLoaderDynamic(*mInstance, reinterpret_cast<PFN_vkGetInstanceProcAddr>(mInstance->getProcAddr("vkGetInstanceProcAddr")));

	mMessenger = mInstance->createDebugUtilsMessengerEXTUnique(createInfo, nullptr, dldi);
#endif
}

void Context::createWindowSurface()
{
	VkSurfaceKHR surface;

	if (auto result = glfwCreateWindowSurface(*mInstance, mWindow, nullptr, &surface); result != VK_SUCCESS)
		throw std::runtime_error("Failed to create window surface");

	mSurface = vk::UniqueSurfaceKHR(surface, vk::ObjectDestroy<vk::Instance, vk::DispatchLoaderStatic>(*mInstance));
}

void Context::pickPhysicalDevice()
{
	auto devices = mInstance->enumeratePhysicalDevices();
	if (devices.empty())
		throw std::runtime_error("Failed to find GPUs with Vulkan support");

	for (const auto& device : devices)
	{
		if (isDeviceSuitable(device, *mSurface))
		{
			mPhysicalDevice = device;
			break;
		}
	}

	if (!mPhysicalDevice)
		throw std::runtime_error("Failed to find a suitable GPU!");

#ifndef NDEBUG
	std::cout << "Current Device: " << mPhysicalDevice.getProperties().deviceName << std::endl;
#endif // NDEBUG

	//mPhyisicalDeviceProperties = static_cast<vk::PhysicalDevice>(physicalDevice).getProperties();
}

void Context::findQueueFamilyIndices()
{
	mQueueFamilyIndices = QueueFamilyIndices::findQueueFamilies(mPhysicalDevice, *mSurface);

	if (!mQueueFamilyIndices.isComplete())
		throw std::runtime_error("Queue family indices is not complete");
}

void Context::createLogicalDevice()
{
	std::vector<vk::DeviceQueueCreateInfo> queueInfo;
	std::vector<int> queueFamilies;
	std::vector<std::vector<float>> queuePriorities;
	
	if (mQueueFamilyIndices.isSingleQueue())
	{
		queueFamilies.emplace_back(mQueueFamilyIndices.generalFamily);

		if (mQueueFamilyIndices.computeQueueIndex > 0)
			queuePriorities.emplace_back(std::vector<float>{1.0f, 1.0f});
		else
			queuePriorities.emplace_back(std::vector<float>{1.0f});
	}
	else
	{
		queueFamilies.emplace_back(mQueueFamilyIndices.generalFamily);
		queuePriorities.emplace_back(std::vector<float>{1.0f});

		queueFamilies.emplace_back(mQueueFamilyIndices.computeFamily);
		queuePriorities.emplace_back(std::vector<float>{1.0f});
	}
		
	for (size_t i = 0; i < queueFamilies.size(); i++)
	{
		vk::DeviceQueueCreateInfo info;
		info.queueFamilyIndex = queueFamilies[i];
		info.queueCount = static_cast<uint32_t>(queuePriorities[i].size());
		info.pQueuePriorities = queuePriorities[i].data();
		queueInfo.emplace_back(info);
	}

	vk::PhysicalDeviceFeatures deviceFeatures;
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.fragmentStoresAndAtomics = VK_TRUE;

	// Create the logical device
	vk::DeviceCreateInfo deviceInfo;
	deviceInfo.pQueueCreateInfos = queueInfo.data();
	deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfo.size());

	deviceInfo.pEnabledFeatures = &deviceFeatures;

#ifdef ENABLE_VALIDATION_LAYERS
	deviceInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
	deviceInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
#endif

	deviceInfo.enabledExtensionCount = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
	deviceInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();

	mDevice = mPhysicalDevice.createDeviceUnique(deviceInfo);

	mGeneralQueue = mDevice->getQueue(mQueueFamilyIndices.generalFamily, 0);
	mComputeQueue = mDevice->getQueue(mQueueFamilyIndices.computeFamily, mQueueFamilyIndices.computeQueueIndex);
}

void Context::createCommandPools()
{
	vk::CommandPoolCreateInfo poolInfo;
	poolInfo.queueFamilyIndex = mQueueFamilyIndices.generalFamily;

	poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	mStaticCommandPool = mDevice->createCommandPoolUnique(poolInfo);

	poolInfo.flags |= vk::CommandPoolCreateFlagBits::eTransient;
	mDynamicCommandPool = mDevice->createCommandPoolUnique(poolInfo);
	
	poolInfo.queueFamilyIndex = mQueueFamilyIndices.computeFamily;
	mComputeCommandPool = mDevice->createCommandPoolUnique(poolInfo);
}
