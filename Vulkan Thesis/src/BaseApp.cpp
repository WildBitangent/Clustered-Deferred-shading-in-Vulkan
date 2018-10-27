#include "BaseApp.h"
#include "Context.h"

#define GLFW_INCLUDE_VULKAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <GLFW/glfw3.h>
#include <chrono>

#include "Util.h"
#include <iostream>


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
			static size_t counter = 0;

			// if (counter++ % 60 == 0)
			// 	std::cout 
			// 		<< mCamera.position.x << " " << mCamera.position.y << " " << mCamera.position.z << " : " 
			// 		<< mCamera.rotation.w << " " << mCamera.rotation.x << " " << mCamera.rotation.y << " " << mCamera.rotation.z 
			// 		<< std::endl;

			mRenderer.setCamera(mCamera.getViewMatrix(), mCamera.position);
			mRenderer.requestDraw(1.f);
			mRenderer.cleanUp();
		}
		
	}
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
{
	
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
		mCamera.position += mCamera.rotation  * glm::vec3(0.0f, 0.0f, -1.0f) * mCamera.moveSpeed * dt;

	if (mSDown)
		mCamera.position -= mCamera.rotation  * glm::vec3(0.0f, 0.0f, -1.0f) * mCamera.moveSpeed * dt;

	if (mADown)
		mCamera.position -= mCamera.rotation  * glm::vec3(1.0f, 0.0f, 0.0f) * mCamera.moveSpeed * dt;

	if (mDDown)
		mCamera.position += mCamera.rotation  * glm::vec3(1.0f, 0.0f, 0.0f) * mCamera.moveSpeed * dt;
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
		}
	}

	mUI.onKeyPress(key, action);
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
