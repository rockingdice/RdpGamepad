// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"

#include "ViGEmInterface.h"

#pragma comment(lib, "setupapi.lib")

namespace {

constexpr uint8_t DS4_DPAD_MASK[] = {
	XINPUT_GAMEPAD_DPAD_UP,
	XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT,
	XINPUT_GAMEPAD_DPAD_RIGHT,
	XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT,
	XINPUT_GAMEPAD_DPAD_DOWN,
	XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,
	XINPUT_GAMEPAD_DPAD_LEFT,
	XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT,
};

constexpr uint16_t DS4_BUTTON_MASK[] = {
	XINPUT_GAMEPAD_X,
	XINPUT_GAMEPAD_A,
	XINPUT_GAMEPAD_B,
	XINPUT_GAMEPAD_Y,
	XINPUT_GAMEPAD_LEFT_SHOULDER,
	XINPUT_GAMEPAD_RIGHT_SHOULDER,
	0,
	0,
	XINPUT_GAMEPAD_BACK,
	XINPUT_GAMEPAD_START,
	XINPUT_GAMEPAD_LEFT_THUMB,
	XINPUT_GAMEPAD_RIGHT_THUMB,
};

constexpr uint16_t X360_DPAD_MASK[] = {
	XUSB_GAMEPAD_DPAD_UP,
	XUSB_GAMEPAD_DPAD_UP | XUSB_GAMEPAD_DPAD_RIGHT,
	XUSB_GAMEPAD_DPAD_RIGHT,
	XUSB_GAMEPAD_DPAD_DOWN | XUSB_GAMEPAD_DPAD_RIGHT,
	XUSB_GAMEPAD_DPAD_DOWN,
	XUSB_GAMEPAD_DPAD_DOWN | XUSB_GAMEPAD_DPAD_LEFT,
	XUSB_GAMEPAD_DPAD_LEFT,
	XUSB_GAMEPAD_DPAD_UP | XUSB_GAMEPAD_DPAD_LEFT,
	0,
};

constexpr uint16_t X360_BUTTON_MASK[] = {
	XUSB_GAMEPAD_X,
	XUSB_GAMEPAD_A,
	XUSB_GAMEPAD_B,
	XUSB_GAMEPAD_Y,
	XUSB_GAMEPAD_LEFT_SHOULDER,
	XUSB_GAMEPAD_RIGHT_SHOULDER,
	0,
	0,
	XUSB_GAMEPAD_BACK,
	XUSB_GAMEPAD_START,
	XUSB_GAMEPAD_LEFT_THUMB,
	XUSB_GAMEPAD_RIGHT_THUMB
};

uint8_t ToDS4Dpad(uint16_t buttonMask)
{
	auto N = (buttonMask & XINPUT_GAMEPAD_DPAD_UP)    == XINPUT_GAMEPAD_DPAD_UP;
	auto S = (buttonMask & XINPUT_GAMEPAD_DPAD_DOWN)  == XINPUT_GAMEPAD_DPAD_DOWN;
	auto W = (buttonMask & XINPUT_GAMEPAD_DPAD_LEFT)  == XINPUT_GAMEPAD_DPAD_LEFT;
	auto E = (buttonMask & XINPUT_GAMEPAD_DPAD_RIGHT) == XINPUT_GAMEPAD_DPAD_RIGHT;

	uint8_t result = PAD_BUTTON_DPAD_NONE;

	if (N && E)
	{ result = PAD_BUTTON_DPAD_NORTHEAST; }
	else if (N && W)
	{ result = PAD_BUTTON_DPAD_NORTHWEST; }
	else if (S && E)
	{ result = PAD_BUTTON_DPAD_SOUTHEAST; }
	else if (S && W)
	{ result = PAD_BUTTON_DPAD_SOUTHWEST; }
	else if (N)
	{ result = PAD_BUTTON_DPAD_NORTH; }
	else if (S)
	{ result = PAD_BUTTON_DPAD_SOUTH; }
	else if (W)
	{ result = PAD_BUTTON_DPAD_WEST; }
	else if (E)
	{ result = PAD_BUTTON_DPAD_EAST; }

	return result;
}

} // namespace

ViGEmTarget360::ViGEmTarget360(std::shared_ptr<ViGEmClient> Client)
{
	mClient = Client;
	mTarget = vigem_target_x360_alloc();
	vigem_target_add(mClient->GetHandle(), mTarget);
	vigem_target_x360_register_notification(mClient->GetHandle(), mTarget, &StaticControllerNotification, this);
}

ViGEmTarget360::~ViGEmTarget360()
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);

	vigem_target_x360_unregister_notification(mTarget);
	vigem_target_remove(mClient->GetHandle(), mTarget);
	vigem_target_free(mTarget);
}

void ViGEmTarget360::SetGamepadState(const XINPUT_GAMEPAD& Gamepad)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);

	XUSB_REPORT report;
	report.wButtons = Gamepad.wButtons;
	report.bLeftTrigger = Gamepad.bLeftTrigger;
	report.bRightTrigger = Gamepad.bRightTrigger;
	report.sThumbLX = Gamepad.sThumbLX;
	report.sThumbLY = Gamepad.sThumbLY;
	report.sThumbRX = Gamepad.sThumbRX;
	report.sThumbRY = Gamepad.sThumbRY;
	vigem_target_x360_update(mClient->GetHandle(), mTarget, report);
}

bool ViGEmTarget360::GetVibration(XINPUT_VIBRATION& OutVibration)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);
	if (mHasPendingVibration)
	{
		OutVibration = mPendingVibration;
		mHasPendingVibration = false;
		return true;
	}
	return false;
}

void ViGEmTarget360::SetGamepadState(const PadState& Gamepad)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);

	uint16_t button = 0;
	uint8_t dpad = uint8_t(Gamepad.Buttons & 0xf);
	if (dpad > 8)
	{ dpad = 8; }

	button |= X360_DPAD_MASK[dpad];

	for(auto i=0; i<12; ++i)
	{
		if (X360_BUTTON_MASK[i] == 0)
		{ continue; }

		auto mask = (1 << (i+4));
		if ((Gamepad.Buttons & mask) == mask)
		{ button |= X360_BUTTON_MASK[i]; }
	}

	XUSB_REPORT report;
	report.sThumbLX      = int16_t( ((float(Gamepad.StickL.X) / float(255.0f)) * 2.0f - 1.0f) * SHRT_MAX);
	report.sThumbLY      = int16_t(-((float(Gamepad.StickL.Y) / float(255.0f)) * 2.0f - 1.0f) * SHRT_MAX);
	report.sThumbRX      = int16_t( ((float(Gamepad.StickR.X) / float(255.0f)) * 2.0f - 1.0f) * SHRT_MAX);
	report.sThumbRY      = int16_t(-((float(Gamepad.StickR.Y) / float(255.0f)) * 2.0f - 1.0f) * SHRT_MAX);
	report.bLeftTrigger  = Gamepad.AnalogButtons.L2;
	report.bRightTrigger = Gamepad.AnalogButtons.R2;
	report.wButtons      = button;

	vigem_target_x360_update(mClient->GetHandle(), mTarget, report);
}

bool ViGEmTarget360::GetVibration(PadVibrationParam& OutVibration)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);
	if (mHasPendingVibration)
	{
		OutVibration.LargeMotor = uint8_t((mPendingVibration.wLeftMotorSpeed  / float(USHRT_MAX)) * 0xff);
		OutVibration.SmallMotor = uint8_t((mPendingVibration.wRightMotorSpeed / float(USHRT_MAX)) * 0xff);
		mHasPendingVibration = false;
		return true;
	}
	return false;
}

void ViGEmTarget360::StaticControllerNotification(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, UCHAR LedNumber, LPVOID Context)
{
	auto pThis = static_cast<ViGEmTarget360*>(Context);

	std::unique_lock<std::recursive_mutex> lock(pThis->mMutex);
	pThis->mPendingVibration.wLeftMotorSpeed = LargeMotor << 8;
	pThis->mPendingVibration.wRightMotorSpeed = SmallMotor << 8;
	pThis->mHasPendingVibration = true;
}

ViGEmTargetDS4::ViGEmTargetDS4(std::shared_ptr<ViGEmClient> Client)
{
	mClient = Client;
	mTarget = vigem_target_ds4_alloc();
	vigem_target_add(mClient->GetHandle(), mTarget);
	vigem_target_ds4_register_notification(mClient->GetHandle(), mTarget, &StaticControllerNotification, this);
}

ViGEmTargetDS4::~ViGEmTargetDS4()
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);

	vigem_target_ds4_unregister_notification(mTarget);
	vigem_target_remove(mClient->GetHandle(), mTarget);
	vigem_target_free(mTarget);
}

void ViGEmTargetDS4::SetGamepadState(const XINPUT_GAMEPAD& Gamepad)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);

	uint8_t dpad = ToDS4Dpad(Gamepad.wButtons);

	uint16_t button = dpad;
	for(auto i=0; i<12; ++i)
	{
		if (DS4_BUTTON_MASK[i] == 0)
		{ continue; }

		if ((Gamepad.wButtons & DS4_BUTTON_MASK[i]) == DS4_BUTTON_MASK[i])
		{ button |= (1 << (i + 4)); }
	}

	if (Gamepad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
	{ button |= DS4_BUTTON_TRIGGER_LEFT; }

	if (Gamepad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
	{ button |= DS4_BUTTON_TRIGGER_RIGHT; }

	uint8_t special_button = 0;	// TODO.

	DS4_REPORT report = {};
	report.bThumbLX  = static_cast<uint8_t>( ((Gamepad.sThumbLX + SHRT_MAX) / float(USHRT_MAX)) * 0xff);	// left = 0, right = 255.
	report.bThumbLY  = static_cast<uint8_t>(-((Gamepad.sThumbLY - SHRT_MAX) / float(USHRT_MAX)) * 0xff);	// up   = 0, down  = 255.
	report.bThumbRX  = static_cast<uint8_t>( ((Gamepad.sThumbRX + SHRT_MAX) / float(USHRT_MAX)) * 0xff);	// left = 0, right = 255.
	report.bThumbRY  = static_cast<uint8_t>(-((Gamepad.sThumbRY - SHRT_MAX) / float(USHRT_MAX)) * 0xff);	// up   = 0, down  = 255.
	report.wButtons  = button;
	report.bSpecial  = special_button;
	report.bTriggerL = Gamepad.bLeftTrigger;
	report.bTriggerR = Gamepad.bRightTrigger;

	vigem_target_ds4_update(mClient->GetHandle(), mTarget, report);
}

void ViGEmTargetDS4::SetGamepadState(const PadState& State)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);

	DS4_REPORT report = {};
	report.bThumbLX  = State.StickL.X;
	report.bThumbLY  = State.StickL.Y;
	report.bThumbRX  = State.StickR.X;
	report.bThumbRY  = State.StickR.Y;
	report.wButtons  = State.Buttons;
	report.bSpecial  = State.SpecialButtons;
	report.bTriggerL = State.AnalogButtons.L2;
	report.bTriggerR = State.AnalogButtons.R2;

	vigem_target_ds4_update(mClient->GetHandle(), mTarget, report);
}

bool ViGEmTargetDS4::GetVibration(XINPUT_VIBRATION& OutVibration)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);
	if (mHasPendingVibration)
	{
		OutVibration.wLeftMotorSpeed  = mPendingLargeMotor;
		OutVibration.wRightMotorSpeed = mPendingSmallMotor;
		mHasPendingVibration = false;
		return true;
	}
	return false;
}

bool ViGEmTargetDS4::GetVibration(PadVibrationParam& OutVibration)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);
	if (mHasPendingVibration)
	{
		OutVibration.LargeMotor = mPendingLargeMotor;
		OutVibration.SmallMotor = mPendingSmallMotor;
		mHasPendingVibration = false;
		return true;
	}
	return false;
}

bool ViGEmTargetDS4::GetLightBarColor(PadColor& OutLightBarColor)
{
	std::unique_lock<std::recursive_mutex> lock(mMutex);
	if (mHasPendingLightBar)
	{
		OutLightBarColor.R = mPendingLightBarR;
		OutLightBarColor.G = mPendingLightBarG;
		OutLightBarColor.B = mPendingLightBarG;
		mHasPendingLightBar = false;
		return true;
	}
	return false;
}

void ViGEmTargetDS4::StaticControllerNotification(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, DS4_LIGHTBAR_COLOR LightBarColor, LPVOID UserData)
{
	auto pThis = static_cast<ViGEmTargetDS4*>(UserData);

	std::unique_lock<std::recursive_mutex> lock(pThis->mMutex);
	pThis->mPendingLargeMotor	= LargeMotor;
	pThis->mPendingSmallMotor	= SmallMotor;
	pThis->mPendingLightBarR	= LightBarColor.Red;
	pThis->mPendingLightBarG	= LightBarColor.Green;
	pThis->mPendingLightBarB	= LightBarColor.Blue;
	pThis->mHasPendingVibration	= true;
	pThis->mHasPendingLightBar  = true;
}

ViGEmClient::ViGEmClient()
{
	mClient = vigem_alloc();
	vigem_connect(mClient);
}

ViGEmClient::~ViGEmClient()
{
	vigem_free(mClient);
}

std::unique_ptr<ViGEmTarget360> ViGEmClient::CreateControllerAs360()
{
	return std::unique_ptr<ViGEmTarget360>{new ViGEmTarget360{shared_from_this()}};
}

std::unique_ptr<ViGEmTargetDS4> ViGEmClient::CreateControllerAsDS4()
{
	return std::unique_ptr<ViGEmTargetDS4>{new ViGEmTargetDS4{shared_from_this()}}; 
}
