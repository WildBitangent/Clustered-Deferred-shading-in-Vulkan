#include "Scene.h"
#include "Renderer.h"

Scene::Scene(SceneConfig config, Renderer& renderer, ThreadPool& threadPool, GLFWwindow* window)
	: mCamera(window, config.camPosition, config.camRotation)
	, mScale(config.scale)
{ 
	mModel.loadModel(renderer.mContext, config.modelPath, *renderer.mSampler, *renderer.mDescriptorPool, renderer.mResource, threadPool);
}

void Scene::update(float dt)
{
	mCamera.update(dt);
}

Camera& Scene::getCamera()
{
	return mCamera;
}

glm::vec3 Scene::getScale() const
{
	return mScale;
}

const std::vector<MeshPart>& Scene::getGeometry() const
{
	return mModel.getMeshParts();
}

const std::vector<SceneConfig> SceneConfigurations::data = 
{
	{
		"Sponza",
		"data/models/sponza/sponza.obj", 
		{ 13.0f, 1.5f, -0.45f }, 
		{ 0.68f, 0.0f, 0.72f, 0.0f },
		glm::vec3(0.01f),
		{-15.f, -15.f, -15.f},
		{15.f, 15.f, 15.f}, // todo
	},
	{
		"San Miguel",
		"data/models/San_Miguel/san-miguel.obj",
		{ 13.0f, 1.5f, -0.45f }, 
		{ 0.68f, 0.0f, 0.72f, 0.0f },
		glm::vec3(0.7f),
		{-15.f, -15.f, -15.f},
		{15.f, 15.f, 15.f}, // todo
	},
	{
		"Bridge",
		"data/models/bridge/model.obj",
		{ 13.0f, 1.5f, -0.45f }, 
		{ 0.68f, 0.0f, 0.72f, 0.0f },
		glm::vec3(1.0f),
		{-15.f, -15.f, -15.f},
		{15.f, 15.f, 15.f}, // todo
	},
	{
		"dust2",
		"data/models/dust2/de_dust2.obj",
		{ 13.0f, 1.5f, -0.45f }, 
		{ 0.68f, 0.0f, 0.72f, 0.0f },
		glm::vec3(0.01f),
		{-31, -5.5, -37.5},
		{18.5, 15.5, 16},
	},
	{
		"Vokselia",
		"data/models/vokselia_spawn/vokselia_spawn.obj",
		{ 13.0f, 1.5f, -0.45f }, 
		{ 0.68f, 0.0f, 0.72f, 0.0f },
		glm::vec3(15.7f),
		{-15.f, -15.f, -15.f},
		{15.f, 15.f, 15.f}, // todo
	},
	{ // todo transparency
		"Sibenik",
		"data/models/sibenik/sibenik.obj",
		{ 13.0f, 1.5f, -0.45f }, 
		{ 0.68f, 0.0f, 0.72f, 0.0f },
		glm::vec3(1.0f),
		{-15.f, -15.f, -15.f},
		{15.f, 15.f, 15.f}, // todo
	},
	{
		"Rungholt",
		"data/models/rungholt/house.obj",
		{ 13.0f, 1.5f, -0.45f }, 
		{ 0.68f, 0.0f, 0.72f, 0.0f },
		glm::vec3(0.7f),
		{-15.f, -15.f, -15.f},
		{15.f, 15.f, 15.f}, // todo
	},
};