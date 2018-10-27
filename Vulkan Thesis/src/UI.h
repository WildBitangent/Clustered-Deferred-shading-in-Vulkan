#pragma once
#include <string>

enum class DebugStates : unsigned
{
	disabled,
	albedo,
	normal,
	specular,
	position,

	count
};

inline DebugStates& operator++(DebugStates& s) 
{
	using type = std::underlying_type<DebugStates>::type;
	s = static_cast<DebugStates>((static_cast<type>(s) + 1) % static_cast<type>(DebugStates::count));
	return s;
}

inline DebugStates& operator--(DebugStates& s)
{
	using type = std::underlying_type<DebugStates>::type; // TODO correct it
	s = static_cast<DebugStates>((static_cast<type>(s) - 1) % static_cast<type>(DebugStates::count));
	return s;
}

class UI
{
public:

	void onKeyPress(int key, int action);
	DebugStates getDebugIndex() const;
	bool debugStateUniformNeedsUpdate();

private:
	DebugStates mDebugIndex = DebugStates::disabled;
	bool mDebugStateDirtyBit = false;
};