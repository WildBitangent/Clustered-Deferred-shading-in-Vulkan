#include "Camera.h"
#include <GLFW/glfw3.h>

namespace
{
	bool isNearlyEqual(float a, float b, float tolerance = 1e-8f)
	{
		return glm::abs(a - b) <= tolerance;
	}
}


Camera::Camera(GLFWwindow* window, glm::vec3 position, glm::quat rotation)
	: mPosition(position)
	, mRotation(rotation)
{
	glfwGetFramebufferSize(window, &mExtent.x, &mExtent.y);

	auto cursorposCallback = [](GLFWwindow* window, double xPos, double yPos)
	{
		reinterpret_cast<Camera*>(glfwGetWindowUserPointer(window))->onCursorPosChange(window, xPos, yPos);
	};

	auto mousebuttonCallback = [](GLFWwindow* window, int button, int action, int mods)
	{
		reinterpret_cast<Camera*>(glfwGetWindowUserPointer(window))->onMouseButton(window, button, action, mods);
	};

	auto keyCallback = [](GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		reinterpret_cast<Camera*>(glfwGetWindowUserPointer(window))->onKeyPress(window, key, scancode, action, mods);
	};
	
	glfwSetMouseButtonCallback(window, mousebuttonCallback);
	glfwSetCursorPosCallback(window, cursorposCallback);
	glfwSetKeyCallback(window, keyCallback);
}

void Camera::update(float dt)
{
	glfwPollEvents();
		
	if (mKeyPressed[GLFW_MOUSE_BUTTON_RIGHT])
	{
		auto cursorDelta = (mCursorPos - mPrevCursorPos) / glm::vec2(glm::min(mExtent.x, mExtent.y) * 2.0f);
	
		if (!isNearlyEqual(cursorDelta.x, 0, 1e-5))
			mRotation = glm::angleAxis(mRotationSpeed * -cursorDelta.x, glm::vec3(0.0f, 1.0f, 0.0f)) * mRotation;
	
		if (!isNearlyEqual(cursorDelta.y, 0, 1e-5))
			mRotation = mRotation * glm::angleAxis(mRotationSpeed * -cursorDelta.y, glm::vec3(1.0f, 0.0f, 0.0f));
	
		mRotation = glm::normalize(mRotation);
		mPrevCursorPos = mCursorPos;
	}

	if (mKeyPressed[GLFW_KEY_W])
		mPosition += mRotation * glm::vec3(0.0f, 0.0f, -1.0f) * mMoveSpeed * dt;

	if (mKeyPressed[GLFW_KEY_S])
		mPosition -= mRotation * glm::vec3(0.0f, 0.0f, -1.0f) * mMoveSpeed * dt;

	if (mKeyPressed[GLFW_KEY_A])
		mPosition -= mRotation * glm::vec3(1.0f, 0.0f, 0.0f) * mMoveSpeed * dt;

	if (mKeyPressed[GLFW_KEY_D])
		mPosition += mRotation * glm::vec3(1.0f, 0.0f, 0.0f) * mMoveSpeed * dt;
}

void Camera::setWindowExtent(glm::uvec2 extent)
{
	mExtent = extent;
}

glm::mat4 Camera::getViewMatrix() const
{
	return glm::transpose(glm::toMat4(mRotation)) * glm::translate(glm::mat4(1.0f), -mPosition);
}

glm::vec3 Camera::getPosition() const
{
	return mPosition;
}

void Camera::onMouseButton(GLFWwindow* window, int button, int action, int mods)
{
	if (action == GLFW_PRESS) 
	{
		mKeyPressed[button] = true;
		if (button == GLFW_MOUSE_BUTTON_RIGHT)
		{
			mFirstMouse = true;
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
	}
	else if (action == GLFW_RELEASE)
	{
		mKeyPressed[button] = false;
		if (button == GLFW_MOUSE_BUTTON_RIGHT)
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

void Camera::onKeyPress(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS)
		mKeyPressed[key] = true;
	else if (action == GLFW_RELEASE)
		mKeyPressed[key] = false;
}

void Camera::onCursorPosChange(GLFWwindow* window, double xPos, double yPos)
{
	if (mKeyPressed[GLFW_MOUSE_BUTTON_RIGHT])
	{
		mCursorPos = { xPos, yPos };

		if(mFirstMouse)
	    {
			mPrevCursorPos = mCursorPos;
			mFirstMouse = false;
	    }
	}
}
