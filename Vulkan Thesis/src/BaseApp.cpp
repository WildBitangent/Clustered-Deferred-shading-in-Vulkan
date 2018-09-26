#include "BaseApp.h"
#include "Context.h"

#define GLFW_INCLUDE_VULKAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <GLFW/glfw3.h>

BaseApp& BaseApp::getInstance()
{
	static BaseApp instance;
	return instance;
}

void BaseApp::run()
{
	while (!glfwWindowShouldClose(mWindow))
	{
		glfwPollEvents();

		mRenderer.requestDraw(1.f);
		mRenderer.cleanUp();		
	}
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

	return window;
}
