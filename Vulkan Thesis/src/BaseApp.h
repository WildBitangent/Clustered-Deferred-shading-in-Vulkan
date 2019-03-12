#pragma once
#include "Context.h"
#include "Renderer.h"
#include "UI.h"

#include <glm/gtx/quaternion.hpp>

struct Camera
{
	glm::vec3 position = { 0.0f, 0.0f, 0.0f };
	// glm::vec3 position = { 13.0f, 1.5f, -0.45f };
	//glm::quat rotation = { 0.68f, 0.0f, 0.72f, 0.0f }; 
	glm::quat rotation = { 0.0f, 0.0f, 0.0f, 0.0f }; 
	float rotationSpeed = glm::pi<float>();
	float moveSpeed = 10.f;

	glm::mat4 getViewMatrix() const
	{
		return glm::transpose(glm::toMat4(rotation)) * glm::translate(glm::mat4(1.0f), -position);
	}
};

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
	//~BaseApp();

	GLFWwindow* createWindow();
	void tick(float dt);

	void onMouseButton(int button, int action, int mods);
	void onKeyPress(int key, int scancode, int action, int mods);
	void onCursorPosChange(double xPos, double yPos);

private:
	Camera mCamera;
	std::vector<PointLight> mLights; // TODO prob move this somewhere else
	std::vector<glm::vec3> mLightsDirections;
	std::vector<float> mSpeeds;

	bool mFirstMouse = true;
	glm::vec2 mCursorPos = { 0, 0 };
	glm::vec2 mPrevCursorPos = { 0,0 };
	float mPitch = 0.0f;
	float mYaw = 0.0f;

	bool mRMBDown = false;
	bool mLMBDown = false;
	bool mWDown = false;
	bool mSDown = false;
	bool mADown = false;
	bool mDDown = false;

	bool mLeftDown = false;
	bool mRightDown = false;
	bool mForwardDown = false;
	bool mBackwardDown = false;
	bool mUpDown = false;
	bool mDownDown = false;


private:
	GLFWwindow* mWindow = createWindow();
	std::unique_ptr<ThreadPool>	mThreadPool; // deferred initialization
	Renderer mRenderer;
	UI mUI;
	
};
