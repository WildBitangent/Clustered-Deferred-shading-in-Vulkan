#pragma once
#include "Context.h"
#include "Renderer.h"

#include <glm/gtx/quaternion.hpp>

struct Camera
{
	glm::vec3 position = { 1.5f, 1.5f, 1.5f };
	glm::quat rotation = { 1.0f, 0.0f, 0.0f, 0.0f };
	float rotationSpeed = glm::pi<float>();
	float moveSpeed = 10.f;

	glm::mat4 getViewMatrix() const
	{
		return glm::transpose(glm::toMat4(rotation)) * glm::translate(glm::mat4(1.0f), -position);
	}
};

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
	void tick(float dt);

	void onMouseButton(int button, int action, int mods);
	void onKeyPress(int key, int scancode, int action, int mods);
	void onCursorPosChange(double xPos, double yPos);

private:
	Camera mCamera;

	glm::vec2 mCursorPos = { 0, 0 };
	glm::vec2 mPrevCursorPos = { 0,0 };

	bool mRMBDown = false;
	bool mLMBDown = false;
	bool mWDown = false;
	bool mSDown = false;
	bool mADown = false;
	bool mDDown = false;

private:
	GLFWwindow* mWindow = createWindow();
	Renderer mRenderer;
};
