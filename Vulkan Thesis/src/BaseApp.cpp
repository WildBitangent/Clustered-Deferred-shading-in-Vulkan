#include "BaseApp.h"
#include "Context.h"

#define GLFW_INCLUDE_VULKAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <GLFW/glfw3.h>
#include <chrono>

#include "Util.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"


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
		deltaTime = std::chrono::duration<float>(current - startTime).count();

		glfwPollEvents();

		if (deltaTime > 1.0f / 60.0f)
		{
			tick(deltaTime);
			startTime = current;


			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			mUI.update();

			ImGui::Render();
			mUI.recordCommandBuffer();

			mRenderer.setCamera(mCamera.getViewMatrix(), mCamera.position);
			mRenderer.updateLights(mLights);
			mRenderer.requestDraw(1.f);
			// mRenderer.cleanUp();
		}
		else
		{
			mRenderer.requestDraw(1.f);
		}
		
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

BaseApp::BaseApp()
	: mRenderer(mWindow)
	, mUI(mWindow, mRenderer)
{
	mLights.reserve(50'000); // todo refactor for max tile lights
	mLightsDirections.reserve(50'000);

	// create lights
	for (size_t i = 0; i < 50'000; i++)
	{
		mLights.emplace_back(PointLight{
			{ 11.0f - i * 3, 1.5, -0.4 },
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
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	auto window = glfwCreateWindow(1024, 726, "Vulkan test", nullptr, nullptr);

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

	glfwSetCursorPosCallback(window, cursorposCallback);
	glfwSetMouseButtonCallback(window, mousebuttonCallback);
	glfwSetKeyCallback(window, keyCallback);

	return window;
}

void BaseApp::tick(float dt)
{
	auto randVec3 = []()
	{
		return glm::normalize(glm::vec3(rand() - RAND_MAX / 2, rand() - RAND_MAX / 2, rand() - RAND_MAX / 2));
	};

	if (mUI.mContext.lightsAnimation)
	{
		for (size_t i = 1; i < mUI.mContext.lightsCount; i++)
		{
			if (glm::length(mLights[i].position) > 25)
			{
				mLightsDirections[i] = randVec3();
				mLights[i].intensity = glm::abs(randVec3());
				mLights[i].position = randVec3() * glm::vec3(20, 0, 20);
				mSpeeds[i] = (1 + (rand() % 100)) / 200.0f;
			}

			mLights[i].position += mLightsDirections[i] * mSpeeds[i];
		}
	}


	if (mRMBDown)
	{
		auto cursorDelta = (mCursorPos - mPrevCursorPos) / glm::vec2(glm::min(1024, 726) * 2.0f);

		if (!util::isNearlyEqual(cursorDelta.x, 0))
			mCamera.rotation = glm::angleAxis(mCamera.rotationSpeed * -cursorDelta.x, glm::vec3(0.0f, 1.0f, 0.0f)) * mCamera.rotation;

		if (!util::isNearlyEqual(cursorDelta.y, 0))
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
		double x, y;
		glfwGetCursorPos(mWindow, &x, &y);
		mCursorPos = { x, y };

		if (button == GLFW_MOUSE_BUTTON_RIGHT)
		{
			mRMBDown = true;
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
			mRenderer.reloadShaders();
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
	if (!mLMBDown && !mRMBDown)
		return;

	if (mLMBDown) 
		mCursorPos = { xPos, yPos };
	else if (mRMBDown) 
		mCursorPos = { xPos, yPos };
}
