#include "UI.h"
#include <GLFW/glfw3.h>
#include "BaseApp.h"

void UI::onKeyPress(int key, int action)
{
	if (action == GLFW_PRESS)
	{
		switch (key)
		{
			// todo use a hash map or something
		case GLFW_KEY_PAGE_UP:
			++mDebugIndex;
			mDebugStateDirtyBit = true;
			break;
		case GLFW_KEY_PAGE_DOWN:
			--mDebugIndex;
			mDebugStateDirtyBit = true;
			break;

		case GLFW_KEY_ENTER:
			BaseApp::getInstance().getRenderer().reloadShaders();
			break;
		}
	}
}

DebugStates UI::getDebugIndex() const
{
	return mDebugIndex;
}

bool UI::debugStateUniformNeedsUpdate()
{
	auto ret = mDebugStateDirtyBit;
	mDebugStateDirtyBit = false;
	return ret;
}
