/**
 * @file 'Scene.h'
 * @brief Scene class and confirugartions
 * @copyright The MIT license 
 * @author Matej Karas
 */

#pragma once
#include "Model.h"
#include "Camera.h"
#include "ThreadPool.h"

class Renderer;

struct SceneConfig
{
	std::string sceneName;

	std::string modelPath;
	glm::vec3 camPosition;
	glm::quat camRotation;
	glm::vec3 scale;
	glm::vec3 lightExtentMin;
	glm::vec3 lightExtentMax;
};

class Scene
{
public:
	Scene(SceneConfig config, Renderer& renderer, ThreadPool& threadPool, GLFWwindow* window);
	
	Scene() = default;
	~Scene() = default;

	Scene(Scene&) = delete;
	Scene(Scene&&) = default;
	Scene& operator=(Scene&) = delete;
	Scene& operator=(Scene&&) = default;

	void update(float dt);

	Camera& getCamera();
	glm::vec3 getScale() const;
	const std::vector<MeshPart>& getGeometry() const;

private:
	Model mModel;
	Camera mCamera;
	glm::vec3 mScale;
};


namespace SceneConfigurations
{
	extern const std::vector<SceneConfig> data;

	inline auto nameGetter = [](void* _, int idx, const char** outText)
	{		
		if (idx < 0 || idx >= static_cast<int>(data.size()))
			return false;

		*outText = data[idx].sceneName.c_str();
		return true;
	};
}
