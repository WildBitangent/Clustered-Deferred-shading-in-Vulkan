/**
 * @file 'BaseApp.h'
 * @brief Base for application
 * @copyright The MIT license 
 * @author Matej Karas
 */

#pragma once
#include "Context.h"
#include "Renderer.h"
#include "UI.h"

#include <unordered_map>

#define MAX_LIGHTS 500'000

struct PointLight
{
	glm::vec3 position;
	float radius;
	glm::vec3 intensity;
	float padding;
};

class BaseApp
{
public:
	BaseApp(const BaseApp&) = delete;
	void operator=(const BaseApp&) = delete;

	static BaseApp& getInstance();

	void run();

	UI& getUI();
	Renderer& getRenderer();
	ThreadPool& getThreadPool();

private:
	BaseApp();

	GLFWwindow* createWindow();
	void createScene();
	void updateLights(float dt);

private:
	std::vector<PointLight> mLights;
	std::vector<glm::vec3> mLightsDirections;
	// std::vector<float> mSpeeds;
	
private:
	GLFWwindow* mWindow = createWindow();
	std::unique_ptr<ThreadPool>	mThreadPool; // deferred initialization
	Renderer mRenderer;
	UI mUI;
	Scene mScene;
};
