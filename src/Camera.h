/**
 * @file 'Camera.h'
 * @brief Camera defintions
 * @copyright The MIT license 
 * @author Matej Karas
 */

#pragma once
#include <unordered_map>
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

class Camera
{
public:
	Camera() = default;
	Camera(GLFWwindow* window, glm::vec3 position, glm::quat rotation);

	void update(float dt);

	void setWindowExtent(glm::uvec2 extent); // todo update on resize
	glm::mat4 getViewMatrix() const;
	glm::vec3 getPosition() const;

private:
	void onMouseButton(GLFWwindow* window, int button, int action, int mods);
	void onKeyPress(GLFWwindow* window, int key, int scancode, int action, int mods);
	void onCursorPosChange(GLFWwindow* window, double xPos, double yPos);

private:
	std::unordered_map<int, bool> mKeyPressed;
	glm::vec2 mCursorPos = { 0, 0 };
	glm::vec2 mPrevCursorPos = { 0,0 };
	bool mFirstMouse = true;

	glm::ivec2 mExtent;

	glm::vec3 mPosition;
	glm::quat mRotation; 
	float mRotationSpeed = glm::pi<float>();
	float mMoveSpeed = 7.f;
};
