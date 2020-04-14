// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"

#include "ViGEmInterface.h"

#pragma comment(lib, "setupapi.lib")

namespace {

constexpr uint8_t DPAD_MASK[] = {
	XINPUT_GAMEPAD_DPAD_UP,
	XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT,
	XINPUT_GAMEPAD_DPAD_RIGHT,
	XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT,
	XINPUT_GAMEPAD_DPAD_DOWN,
	XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,
	XINPUT_GAMEPAD_DPAD_LEFT,
	XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT,
};

constexpr uint16_t BUTTON_MASK[] = {
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
	std::unique_lock<std::mutex> lock(mMutex);

	vigem_target_x360_unregister_notification(mTarget);
	vigem_target_remove(mClient->GetHandle(), mTarget);
	vigem_target_free(mTarget);
}

void ViGEmTarget360::SetGamepadState(const XINPUT_GAMEPAD& Gamepad)
{
	std::unique_lock<std::mutex> lock(mMutex);

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
	std::unique_lock<std::mutex> lock(mMutex);
	if (mHasPendingVibration)
	{
		OutVibration = mPendingVibration;
		mHasPendingVibration = false;
		return true;
	}
	return false;
}

void ViGEmTarget360::StaticControllerNotification(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, UCHAR LedNumber, LPVOID Context)
{
	auto pThis = static_cast<ViGEmTarget360*>(Context);

	std::unique_lock<std::mutex> lock(pThis->mMutex);
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
	std::unique_lock<std::mutex> lock(mMutex);

	vigem_target_ds4_unregister_notification(mTarget);
	vigem_target_remove(mClient->GetHandle(), mTarget);
	vigem_target_free(mTarget);
}

void ViGEmTargetDS4::SetGamepadState(const XINPUT_GAMEPAD& Gamepad)
{
	std::unique_lock<std::mutex> lock(mMutex);

	uint8_t dpad = 0x8;
	for(auto i=7; i>=0; i--)
	{
		if ((Gamepad.wButtons & DPAD_MASK[i]) == DPAD_MASK[i])
		{
			dpad = i;
			break;
		}
	}

	uint16_t button = dpad;
	for(auto i=0; i<12; ++i)
	{
		if (BUTTON_MASK[i] == 0)
		{ continue; }

		if ((Gamepad.wButtons & BUTTON_MASK[i]) == BUTTON_MASK[i])
		{ button |= (1 << (i + 4)); }
	}

	if (Gamepad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
	{ button |= DS4_BUTTON_TRIGGER_LEFT; }

	if (Gamepad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
	{ button |= DS4_BUTTON_THUMB_RIGHT; }

	uint8_t special_button = 0;	// TODO.

	DS4_REPORT report = {};
	report.bThumbLX  =  (Gamepad.sThumbLX + SHRT_MAX) / 0xff;	// left = 0, right = 255.
	report.bThumbLY  = -(Gamepad.sThumbLY - SHRT_MAX) / 0xff;	// up   = 0, down  = 255.
	report.bThumbRX  =  (Gamepad.sThumbRX + SHRT_MAX) / 0xff;	// left = 0, right = 255.
	report.bThumbRY  = -(Gamepad.sThumbRY - SHRT_MAX) / 0xff;	// up   = 0, down  = 255.
	report.wButtons  = button;
	report.bSpecial  = special_button;
	report.bTriggerL = Gamepad.bLeftTrigger;
	report.bTriggerR = Gamepad.bRightTrigger;

	vigem_target_ds4_update(mClient->GetHandle(), mTarget, report);
}

bool ViGEmTargetDS4::GetVibration(XINPUT_VIBRATION& OutVibration)
{
	std::unique_lock<std::mutex> lock(mMutex);
	if (mHasPendingVibration)
	{
		OutVibration.wLeftMotorSpeed  = mPendingLargeMotor;
		OutVibration.wRightMotorSpeed = mPendingSmallMotor;
		mHasPendingVibration = false;
		return true;
	}
	return false;
}

void ViGEmTargetDS4::StaticControllerNotification(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, DS4_LIGHTBAR_COLOR LightBarColor, LPVOID UserData)
{
	auto pThis = static_cast<ViGEmTargetDS4*>(UserData);

	std::unique_lock<std::mutex> lock(pThis->mMutex);
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
