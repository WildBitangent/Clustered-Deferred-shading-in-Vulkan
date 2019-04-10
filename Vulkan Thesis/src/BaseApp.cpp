#include "BaseApp.h"
#include "Context.h"

#define GLFW_INCLUDE_VULKAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <GLFW/glfw3.h>
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
	float deltaTime;

	while (!glfwWindowShouldClose(mWindow))
	{
		current = std::chrono::high_resolution_clock::now();
		deltaTime = std::chrono::duration<double>(current - startTime).count();

		glfwPollEvents();

		// if (deltaTime > 1.0 / 60.0)
		{
			tick(deltaTime);
			startTime = current;
			
			mRenderer.setCamera(mCamera.getViewMatrix(), mCamera.position);
			mRenderer.updateLights(mLights);
			// mRenderer.cleanUp();
		}
		// else if (mUI.mContext.vSync) // TODO bug blinking
			// continue;

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		
		mUI.update();
		ImGui::Render();
		mUI.recordCommandBuffer();

		if (mUI.mContext.shaderReloadDirtyBit)
		{
			mUI.mContext.shaderReloadDirtyBit = false;
			mRenderer.reloadShaders(4 << (mUI.mContext.tileSize + 1));
		}

		mRenderer.requestDraw(1.f);
		
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
	, mRenderer(mWindow, *mThreadPool)
	, mUI(mWindow, mRenderer)
{
	mLights.reserve(500'000); // todo refactor for max tile lights
	mLightsDirections.reserve(500'000);

	// create lights
	for (size_t i = 0; i < 500'000; i++)
	{
		mLights.emplace_back(PointLight{
			{ 6.5f - i * 3, 1.5, -4.0f },
			//{ 11.0f - i * 3, 1.5, -0.4 },
			// { 11.0f, 1.5, -0.4 },
			2.0f,
			{ 1.0f, 1.0f, 1.0f/* - i * 0.2f*/ },
			0.0f,
		});

		mLightsDirections.emplace_back(glm::normalize(glm::vec3(rand(), abs(rand()), rand())));
		mSpeeds.emplace_back((1 + (rand() % 10)) / 10.0f);
	}

	mLights[0].position = { 11.2854, -0.064, 0.0834 };
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

	glfwSetWindowUserPointer(window, this);


	auto cursorposCallback = [](GLFWwindow* window, double xPos, double yPos)
	{
		getInstance().onCursorPosChange(xPos, yPos);
	};

	auto mousebuttonCallback = [](GLFWwindow* window, int button, int action, int mods)
	{
		getInstance().onMouseButton(button, action, mods);
	};

	auto keyCallback = [](GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		getInstance().onKeyPress(key, scancode, action, mods);
	};

	auto resizeCallback = [](GLFWwindow* window, int width, int height)
	{
		getInstance().getRenderer().resize();
	};

	glfwSetCursorPosCallback(window, cursorposCallback);
	glfwSetMouseButtonCallback(window, mousebuttonCallback);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetWindowSizeCallback(window, resizeCallback);

	return window;
}

void BaseApp::tick(float dt)
{
	auto randVec3 = []()
	{
		static thread_local std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
		static thread_local std::minstd_rand generator(reinterpret_cast<unsigned>(&distribution));

		return glm::vec3(distribution(generator), distribution(generator), distribution(generator));
	};

	auto lightsUpdate = [this, &randVec3](size_t offset, size_t size)
	{
		for (size_t i = offset; i < offset + size; i++)
		{
			const auto& p = mLights[i].position;
			const auto& mi = mUI.mContext.lightBoundMin;
			const auto& ma = mUI.mContext.lightBoundMax;
			if (p.x < mi.x || p.x > ma.x ||
				p.y < mi.y || p.y > ma.y ||
				p.z < mi.z || p.z > ma.z)
			{
				auto size = abs(mUI.mContext.lightBoundMin - mUI.mContext.lightBoundMax);

				mLightsDirections[i] = randVec3();
				mLights[i].intensity = glm::abs(randVec3());
				mLights[i].position = mUI.mContext.lightBoundMin + abs(randVec3()) * size;
				mSpeeds[i] = (1 + (rand() % 100)) / 400.0f;
			}

			mLights[i].position += mLightsDirections[i] * mSpeeds[i];
		}
	};

	if (mUI.mContext.lightsAnimation)
	{
		if (mUI.mContext.lightsCount > (1 << 8))
		{
			size_t lightsPerThread = mUI.mContext.lightsCount / std::thread::hardware_concurrency();
			mThreadPool->addWorkMultiplex([&](size_t id)
			{
				const auto offset = id * lightsPerThread + 1;
				const auto lightsCount = (id == std::thread::hardware_concurrency() - 1) ? mUI.mContext.lightsCount - offset : lightsPerThread;

				lightsUpdate(offset, lightsCount);
			});

			mThreadPool->wait();
		}
		else
			lightsUpdate(1, mUI.mContext.lightsCount);
	}


	if (mRMBDown)
	{
		int width, height;
	    glfwGetFramebufferSize(mWindow, &width, &height);

		auto cursorDelta = (mCursorPos - mPrevCursorPos) / glm::vec2(glm::min(width, height) * 2.0f);
	
		if (!util::isNearlyEqual(cursorDelta.x, 0, 1e-5))
			mCamera.rotation = glm::angleAxis(mCamera.rotationSpeed * -cursorDelta.x, glm::vec3(0.0f, 1.0f, 0.0f)) * mCamera.rotation;
	
		if (!util::isNearlyEqual(cursorDelta.y, 0, 1e-5))
			mCamera.rotation = mCamera.rotation * glm::angleAxis(mCamera.rotationSpeed * -cursorDelta.y, glm::vec3(1.0f, 0.0f, 0.0f));
	
		mCamera.rotation = glm::normalize(mCamera.rotation);
	
		mPrevCursorPos = mCursorPos;
	}

	if (mWDown)
		mCamera.position += mCamera.rotation * glm::vec3(0.0f, 0.0f, -1.0f) * mCamera.moveSpeed * dt;

	if (mSDown)
		mCamera.position -= mCamera.rotation * glm::vec3(0.0f, 0.0f, -1.0f) * mCamera.moveSpeed * dt;

	if (mADown)
		mCamera.position -= mCamera.rotation * glm::vec3(1.0f, 0.0f, 0.0f) * mCamera.moveSpeed * dt;

	if (mDDown)
		mCamera.position += mCamera.rotation * glm::vec3(1.0f, 0.0f, 0.0f) * mCamera.moveSpeed * dt;

	glm::vec3 speed = glm::vec3(3.0f);

	if (mForwardDown)
		mLights[0].position += speed * glm::vec3(0.0f, 0.0f, -1.0f) * dt;

	if (mBackwardDown)
		mLights[0].position -= speed * glm::vec3(0.0f, 0.0f, -1.0f) * dt;

	if (mLeftDown)
		mLights[0].position -= speed * glm::vec3(1.0f, 0.0f, 0.0f) * dt;

	if (mRightDown)
		mLights[0].position += speed * glm::vec3(1.0f, 0.0f, 0.0f) * dt;

	if (mUpDown)
		mLights[0].position += speed * glm::vec3(0.0f, 1.0f, 0.0f) * dt;

	if (mDownDown)
		mLights[0].position -= speed * glm::vec3(0.0f, 1.0f, 0.0f) * dt;

}

void BaseApp::onMouseButton(int button, int action, int mods)
{
	if (action == GLFW_PRESS) 
	{
		// double x, y;
		// glfwGetCursorPos(mWindow, &x, &y);
		// mCursorPos = { x, y };

		if (button == GLFW_MOUSE_BUTTON_RIGHT)
		{
			mRMBDown = true;
			mFirstMouse = true;
			glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		else if (button == GLFW_MOUSE_BUTTON_LEFT)
			mLMBDown = true;
	}
	else if (action == GLFW_RELEASE)
	{
		if (button == GLFW_MOUSE_BUTTON_RIGHT)
		{
			mRMBDown = false;
			glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
		else if (button == GLFW_MOUSE_BUTTON_LEFT)
			mLMBDown = false;
	}
}

void BaseApp::onKeyPress(int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS)
	{
		switch (key)
		{
			// todo use a hash map or something
		case GLFW_KEY_W:
			mWDown = true;
			break;
		case GLFW_KEY_S:
			mSDown = true;
			break;
		case GLFW_KEY_A:
			mADown = true;
			break;
		case GLFW_KEY_D:
			mDDown = true;
			break;


		case GLFW_KEY_KP_8:
			mForwardDown = true;
			break;
		case GLFW_KEY_KP_5:
			mBackwardDown = true;
			break;
		case GLFW_KEY_KP_4:
			mLeftDown = true;
			break;
		case GLFW_KEY_KP_6:
			mRightDown = true;
			break;
		case GLFW_KEY_LEFT_SHIFT:
			mUpDown = true;
			break;
		case GLFW_KEY_LEFT_CONTROL:
			mDownDown = true;
			break;


		case GLFW_KEY_ENTER:
			mRenderer.reloadShaders(4 << (mUI.mContext.tileSize + 1));
			mUI.mContext.shaderReloadDirtyBit = false;
			break;
		}
	}
	else if (action == GLFW_RELEASE)
	{
		switch (key)
		{
		case GLFW_KEY_W:
			mWDown = false;
			break;
		case GLFW_KEY_S:
			mSDown = false;
			break;
		case GLFW_KEY_A:
			mADown = false;
			break;
		case GLFW_KEY_D:
			mDDown = false;
			break;

		case GLFW_KEY_KP_8:
			mForwardDown = false;
			break;
		case GLFW_KEY_KP_5:
			mBackwardDown = false;
			break;
		case GLFW_KEY_KP_4:
			mLeftDown = false;
			break;
		case GLFW_KEY_KP_6:
			mRightDown = false;
			break;
		case GLFW_KEY_LEFT_SHIFT:
			mUpDown = false;
			break;
		case GLFW_KEY_LEFT_CONTROL:
			mDownDown = false;
			break;
		}
	}
}

void BaseApp::onCursorPosChange(double xPos, double yPos)
{
	if (mRMBDown)
	{
		mCursorPos = { xPos, yPos };

		if(mFirstMouse)
	    {
			mPrevCursorPos = mCursorPos;
			mFirstMouse = false;
	    }
	}
}
