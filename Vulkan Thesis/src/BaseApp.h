#pragma once
#include <array>
#include "Context.h"
#include "Renderer.h"

class BaseApp
{
public:
	BaseApp(const BaseApp&) = delete;
	void operator=(const BaseApp&) = delete;

	static BaseApp& getInstance();

	void run();

private:
	BaseApp();
	//~BaseApp();

	GLFWwindow* createWindow();

private:
	GLFWwindow* mWindow = createWindow();
	Renderer mRenderer;
};
