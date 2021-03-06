#include "ppsspp_config.h"

#include <climits>
#include <algorithm>

#include "base/NativeApp.h"
#include "Core/Config.h"
#include "Common/CommonWindows.h"
#include "Common/KeyMap.h"
#include "Common/Log.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "OmegaRedinputDevice.h"

// Utilities to dynamically load XInput. Adapted from SDL.

extern GetTouchPadCallback g_getTouchPad;

#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT 4
#endif

// Undocumented. Steam annoyingly grabs this button though....
#define XINPUT_GUIDE_BUTTON 0x400


// Permanent map. Actual mapping happens elsewhere.
static const struct {int from, to;} xinput_ctrl_map[] = {
	{XINPUT_GAMEPAD_A,              NKCODE_BUTTON_A},
	{XINPUT_GAMEPAD_B,              NKCODE_BUTTON_B},
	{XINPUT_GAMEPAD_X,              NKCODE_BUTTON_X},
	{XINPUT_GAMEPAD_Y,              NKCODE_BUTTON_Y},
	{XINPUT_GAMEPAD_BACK,           NKCODE_BUTTON_SELECT},
	{XINPUT_GAMEPAD_START,          NKCODE_BUTTON_START},
	{XINPUT_GAMEPAD_LEFT_SHOULDER,  NKCODE_BUTTON_L1},
	{XINPUT_GAMEPAD_RIGHT_SHOULDER, NKCODE_BUTTON_R1},
	{XINPUT_GAMEPAD_LEFT_THUMB,     NKCODE_BUTTON_THUMBL},
	{XINPUT_GAMEPAD_RIGHT_THUMB,    NKCODE_BUTTON_THUMBR},
	{XINPUT_GAMEPAD_DPAD_UP,        NKCODE_DPAD_UP},
	{XINPUT_GAMEPAD_DPAD_DOWN,      NKCODE_DPAD_DOWN},
	{XINPUT_GAMEPAD_DPAD_LEFT,      NKCODE_DPAD_LEFT},
	{XINPUT_GAMEPAD_DPAD_RIGHT,     NKCODE_DPAD_RIGHT},
	{XINPUT_GUIDE_BUTTON,           NKCODE_HOME},
};

static const unsigned int xinput_ctrl_map_size = sizeof(xinput_ctrl_map) / sizeof(xinput_ctrl_map[0]);

OmegaRedinputDevice::OmegaRedinputDevice() {

}

OmegaRedinputDevice::~OmegaRedinputDevice() {
	
}

struct Stick {
	Stick (float x_, float y_, float scale) : x(x_ * scale), y(y_ * scale) {}
	float x;
	float y;
};

inline float Clampf(float val, float min, float max) {
	if (val < min) return min;
	if (val > max) return max;
	return val;
}

inline float Signf(float val) {
	return (0.0f < val) - (val < 0.0f);
}

inline float LinearMapf(float val, float a0, float a1, float b0, float b1) {
	return b0 + (((val - a0) * (b1 - b0)) / (a1 - a0));
}

static Stick NormalizedDeadzoneFilter(short x, short y, float dz, int idzm, float idz, float st) {
	Stick s(x, y, 1.0f / 32767.0f);

	float magnitude = sqrtf(s.x * s.x + s.y * s.y);
	if (magnitude > dz) {

		// Circle to square mapping (the PSP stick outputs the full -1..1 square of values)
#if 1
		// Looks way better than the old one, below, in the axis tester.
		float sx = s.x;
		float sy = s.y;
		float scaleFactor = sqrtf((sx * sx + sy * sy) / std::max(sx * sx, sy * sy));
		s.x = sx * scaleFactor;
		s.y = sy * scaleFactor;
#else
		if (magnitude > 1.0f) {
			s.x *= 1.41421f;
			s.y *= 1.41421f;
		}
#endif

		// Linear range mapping (used to invert deadzones)
		float md = std::max(dz, idz);

		if (idzm == 1)
		{
			float xSign = Signf(s.x);
			if (xSign != 0.0f) {
				s.x = LinearMapf(s.x, xSign * dz, xSign, xSign * md, xSign * st);
			}
		}
		else if (idzm == 2)
		{
			float ySign = Signf(s.y);
			if (ySign != 0.0f) {
				s.y = LinearMapf(s.y, ySign * dz, ySign, ySign * md, ySign * st);
			}
		}
		else if (idzm == 3)
		{
			float xNorm = s.x / magnitude;
			float yNorm = s.y / magnitude;
			float mapMag = LinearMapf(magnitude, dz, 1.0f, md, st);
			s.x = xNorm * mapMag;
			s.y = yNorm * mapMag;
		}

		s.x = Clampf(s.x, -1.0f, 1.0f);
		s.y = Clampf(s.y, -1.0f, 1.0f);
	} else {
		s.x = 0.0f;
		s.y = 0.0f;
	}
	return s;
}

static bool NormalizedDeadzoneDiffers(short x1, short y1, short x2, short y2, const float dz)
{
	Stick s1(x1, y1, 1.0f / 32767.0f);
	Stick s2(x2, y2, 1.0f / 32767.0f);

	float magnitude1 = sqrtf(s1.x * s1.x + s1.y * s1.y);
	float magnitude2 = sqrtf(s2.x * s2.x + s2.y * s2.y);
	if (magnitude1 > dz || magnitude2 > dz) {
		return x1 != x2 || y1 != y2;
	}
	return false;
}

static bool NormalizedDeadzoneDiffers(u8 x1, u8 x2, const u8 thresh)
{
	if (x1 > thresh || x2 > thresh) {
		return x1 != x2;
	}
	return false;
}

int OmegaRedinputDevice::UpdateState() {

	bool anySuccess = false;
	for (int i = 0; i < XUSER_MAX_COUNT; i++) {
		XINPUT_STATE state;
		ZeroMemory(&state, sizeof(XINPUT_STATE));
		if (check_delay[i]-- > 0)
			continue;
		
		if (g_getTouchPad != nullptr) {
            state = *((_XINPUT_STATE *)g_getTouchPad(0));
			UpdatePad(i, state);
		} else {
			check_delay[i] = 30;
		}
	}

	// If we get XInput, skip the others. This might not actually be a good idea,
	// and was done to avoid conflicts between DirectInput and XInput.
	return anySuccess ? UPDATESTATE_SKIP_PAD : 0;
}

void OmegaRedinputDevice::UpdatePad(int pad, const XINPUT_STATE &state) {
	static bool notified = false;
	if (!notified) {
		notified = true;
		KeyMap::NotifyPadConnected("Xbox 360 Pad");
	}
	ApplyButtons(pad, state);

	const float STICK_DEADZONE = g_Config.fXInputAnalogDeadzone;
	const int STICK_INV_MODE = g_Config.iXInputAnalogInverseMode;
	const float STICK_INV_DEADZONE = g_Config.fXInputAnalogInverseDeadzone;
	const float STICK_SENSITIVITY = g_Config.fXInputAnalogSensitivity;

	if (NormalizedDeadzoneDiffers(prevState[pad].Gamepad.sThumbLX, prevState[pad].Gamepad.sThumbLY, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, STICK_DEADZONE)) {
		Stick left = NormalizedDeadzoneFilter(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, STICK_DEADZONE, STICK_INV_MODE, STICK_INV_DEADZONE, STICK_SENSITIVITY);

		AxisInput axis;
		axis.deviceId = DEVICE_ID_X360_0 + pad;
		axis.axisId = JOYSTICK_AXIS_X;
		axis.value = left.x;
		if (prevState[pad].Gamepad.sThumbLX != state.Gamepad.sThumbLX) {
			NativeAxis(axis);
		}
		axis.axisId = JOYSTICK_AXIS_Y;
		axis.value = left.y;
		if (prevState[pad].Gamepad.sThumbLY != state.Gamepad.sThumbLY) {
			NativeAxis(axis);
		}
	}

	if (NormalizedDeadzoneDiffers(prevState[pad].Gamepad.sThumbRX, prevState[pad].Gamepad.sThumbRY, state.Gamepad.sThumbRX, state.Gamepad.sThumbRY, STICK_DEADZONE)) {
		Stick right = NormalizedDeadzoneFilter(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY, STICK_DEADZONE, STICK_INV_MODE, STICK_INV_DEADZONE, STICK_SENSITIVITY);

		AxisInput axis;
		axis.deviceId = DEVICE_ID_X360_0 + pad;
		axis.axisId = JOYSTICK_AXIS_Z;
		axis.value = right.x;
		if (prevState[pad].Gamepad.sThumbRX != state.Gamepad.sThumbRX) {
			NativeAxis(axis);
		}
		axis.axisId = JOYSTICK_AXIS_RZ;
		axis.value = right.y;
		if (prevState[pad].Gamepad.sThumbRY != state.Gamepad.sThumbRY) {
			NativeAxis(axis);
		}
	}

	if (NormalizedDeadzoneDiffers(prevState[pad].Gamepad.bLeftTrigger, state.Gamepad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD)) {
		AxisInput axis;
		axis.deviceId = DEVICE_ID_X360_0 + pad;
		axis.axisId = JOYSTICK_AXIS_LTRIGGER;
		axis.value = (float)state.Gamepad.bLeftTrigger / 255.0f;
		NativeAxis(axis);
	}

	if (NormalizedDeadzoneDiffers(prevState[pad].Gamepad.bRightTrigger, state.Gamepad.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD)) {
		AxisInput axis;
		axis.deviceId = DEVICE_ID_X360_0 + pad;
		axis.axisId = JOYSTICK_AXIS_RTRIGGER;
		axis.value = (float)state.Gamepad.bRightTrigger / 255.0f;
		NativeAxis(axis);
	}

	prevState[pad] = state;
	check_delay[pad] = 0;
}

void OmegaRedinputDevice::ApplyButtons(int pad, const XINPUT_STATE &state) {
	u32 buttons = state.Gamepad.wButtons;

	u32 downMask = buttons & (~prevButtons[pad]);
	u32 upMask = (~buttons) & prevButtons[pad];
	prevButtons[pad] = buttons;
	
	for (int i = 0; i < xinput_ctrl_map_size; i++) {
		if (downMask & xinput_ctrl_map[i].from) {
			KeyInput key;
			key.deviceId = DEVICE_ID_X360_0 + pad;
			key.flags = KEY_DOWN;
			key.keyCode = xinput_ctrl_map[i].to;
			NativeKey(key);
		}
		if (upMask & xinput_ctrl_map[i].from) {
			KeyInput key;
			key.deviceId = DEVICE_ID_X360_0 + pad;
			key.flags = KEY_UP;
			key.keyCode = xinput_ctrl_map[i].to;
			NativeKey(key);
		}
	}
}
