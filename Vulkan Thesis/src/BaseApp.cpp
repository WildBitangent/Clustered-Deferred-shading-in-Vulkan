#include "BaseApp.h"
#include "Context.h"

#define GLFW_INCLUDE_VULKAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <GLFW/glfw3.h>
#include <glm/gtx/component_wise.hpp>
#include <chrono>

#include "Util.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include <random>


BaseApp& BaseApp::getInstance()
{
	static BaseApp instance;
	return instance;
}

void BaseApp::run()
{
	auto startTime = std::chrono::high_resolution_clock::now();
	auto current = std::chrono::high_resolution_clock::now();
	
	while (!glfwWindowShouldClose(mWindow))
	{
		current = std::chrono::high_resolution_clock::now();
		const auto deltaTime = std::chrono::duration<double>(current - startTime).count();

		// check vsync
		if (deltaTime < 1.0 / 60.0 && mUI.mContext.vSync)
			continue;

		// update
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		
		mUI.update();
		mScene.update(deltaTime);
		updateLights(deltaTime);
				
		if (mUI.mContext.shaderReloadDirtyBit || glfwGetKey(mWindow, GLFW_KEY_ENTER) == GLFW_PRESS)
			mRenderer.reloadShaders(16 << mUI.mContext.tileSize);

		if (mUI.mContext.sceneReload)
			createScene();

		// render
		ImGui::Render();
		mRenderer.draw();
		
		// reset dirty bits
		mUI.mContext.sceneReload = false;
		mUI.mContext.shaderReloadDirtyBit = false;
		
		startTime = current;
	}

	mRenderer.cleanUp();
}

UI& BaseApp::getUI()
{
	return mUI;
}

Renderer& BaseApp::getRenderer()
{
	return mRenderer;
}

ThreadPool& BaseApp::getThreadPool()
{
	return *mThreadPool;
}

BaseApp::BaseApp()
	: mThreadPool(std::make_unique<ThreadPool>())
	, mRenderer(mWindow, mScene)
	, mUI(mWindow, mRenderer)
{
	createScene();

	mLights.reserve(MAX_LIGHTS);
	mLightsDirections.reserve(MAX_LIGHTS);

	// create lights
	for (size_t i = 0; i < MAX_LIGHTS; i++)
	{
		mLights.emplace_back(PointLight{
			glm::vec3(rand(), rand(), rand()),
			2.0f,
			{ 1.0f, 1.0f, 1.0f/* - i * 0.2f*/ },
			0.0f,
		});

		mLightsDirections.emplace_back(glm::normalize(glm::vec3(rand(), rand(), rand())));
	}
}

GLFWwindow* BaseApp::createWindow()
{
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // no OpenGL context
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	// const auto mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	// glfwWindowHint(GLFW_RED_BITS, mode->redBits);
	// glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
	// glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
	// glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
	auto window = glfwCreateWindow(1280, 720, "Clustered deferred shading in Vulkan", /*glfwGetPrimaryMonitor()*/nullptr, nullptr);
	// auto window = glfwCreateWindow(mode->width, mode->height, "Clustered deferred shading in Vulkan", glfwGetPrimaryMonitor(), nullptr);
	//

	auto resizeCallback = [](GLFWwindow* window, int width, int height)
	{
		getInstance().getRenderer().resize();
	};

	glfwSetWindowSizeCallback(window, resizeCallback);

	return window;
}

void BaseApp::createScene()
{
	mScene = Scene(SceneConfigurations::data[mUI.mContext.currentScene], mRenderer, *mThreadPool, mWindow);
	mUI.mContext.lightBoundMin = SceneConfigurations::data[mUI.mContext.currentScene].lightExtentMin;
	mUI.mContext.lightBoundMax = SceneConfigurations::data[mUI.mContext.currentScene].lightExtentMax;
	mRenderer.onSceneChange();

	glfwSetWindowUserPointer(mWindow, &mScene.getCamera());
}

void BaseApp::updateLights(float dt)
{
	auto randVec3 = []()
	{
		static thread_local std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
		static thread_local std::minstd_rand generator(reinterpret_cast<unsigned>(&distribution)); // seed through random memory addres of distribution

		return glm::vec3(distribution(generator), distribution(generator), distribution(generator));
	};

	auto lightsUpdate = [this, &randVec3, dt](size_t offset, size_t size)
	{
		auto lt = [](glm::vec3& l, glm::vec3& r) { return glm::vec3(l.x < r.x, l.y < r.y, l.z < r.z); };
		auto gt = [](glm::vec3& l, glm::vec3& r) { return glm::vec3(l.x > r.x, l.y > r.y, l.z > r.z); };
		auto compOr = [](glm::bvec3 v) { return v.x | v.y | v.z; };

		for (size_t i = offset; i < offset + size; i++)
		{
			auto mask = lt(mLights[i].position, mUI.mContext.lightBoundMin) + gt(mLights[i].position, mUI.mContext.lightBoundMax);
			if (compOr(static_cast<glm::bvec3>(mask)))
			{
				auto size = abs(mUI.mContext.lightBoundMin - mUI.mContext.lightBoundMax);
				
				mLightsDirections[i] = randVec3();
				mLights[i].intensity = glm::abs(randVec3());
				mLights[i].position = mUI.mContext.lightBoundMin + abs(randVec3()) * size;
				mLights[i].position.y = mLightsDirections[i].y > 0.f ? mUI.mContext.lightBoundMin.y : mUI.mContext.lightBoundMax.y;
			}

			mLights[i].position += mLightsDirections[i] * dt * mUI.mContext.lightSpeed;
		}
	};

	if (mUI.mContext.lightSpeed > 0.f)
	{
		if (mUI.mContext.lightsCount > (1 << 8))
		{
			size_t lightsPerThread = mUI.mContext.lightsCount / std::thread::hardware_concurrency();
			mThreadPool->addWorkMultiplex([&](size_t id)
			{
				const auto offset = id * lightsPerThread ;
				const auto lightsCount = (id == std::thread::hardware_concurrency() - 1) ? mUI.mContext.lightsCount - offset : lightsPerThread;

				lightsUpdate(offset, lightsCount);
			});

			mThreadPool->wait();
		}
		else
			lightsUpdate(1, mUI.mContext.lightsCount);
	}
	
	mRenderer.updateLights(mLights);
}
