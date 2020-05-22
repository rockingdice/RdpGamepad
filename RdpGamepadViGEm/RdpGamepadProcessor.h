// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

namespace RdpGamepad
{
	class RdpGamepadVirtualChannel;
}

class ViGEmClient;
class ViGEmTarget360;
class ViGEmTargetDS4;


enum CONTROLLER_TYPE
{
	CONTROLLER_360 = 0,		// XInput ---> XInput.
	CONTROLLER_360_EMU,		// Dual Shock 4 ---> XInput.
	CONTROLLER_DS4,			// Dual Shock 4 ---> Dual Shock 4.
	CONTROLLER_DS4_EMU,		// XInput ---> Dual Shock 4.
};

class RdpGamepadProcessor
{
public:
	RdpGamepadProcessor();
	~RdpGamepadProcessor();

	void Start(CONTROLLER_TYPE type = CONTROLLER_360);
	void Stop();

	CONTROLLER_TYPE GetType() const
	{ return mType; }

	bool IsConnected() const
	{ return mRdpGamepadConnected; }

	DWORD GetErrorCode() const
	{ return mErrorCode; }

private:
	std::unique_ptr<RdpGamepad::RdpGamepadVirtualChannel> mRdpGamepadChannel;
	std::shared_ptr<ViGEmClient> mViGEmClient;
	std::shared_ptr<ViGEmTarget360> mViGEmTarget360;
	std::shared_ptr<ViGEmTargetDS4> mViGEmTargetDS4;
	std::thread mThread;
	std::recursive_mutex mMutex;
	unsigned int mRdpGamepadOpenRetry = 0;
	unsigned int mRdpGamepadPollTicks = 0;
	unsigned int mLastGetStateResponseTicks = 0;
	bool mRdpGamepadConnected = false;
	bool mKeepRunning = false;
	CONTROLLER_TYPE mType = CONTROLLER_360;
	DWORD mErrorCode = S_OK;

	void Run();
	void RdpGamepadTidy();
	void RdpGamepadProcess360();
	void RdpGamepadProcess360Emulate();
	void RdpGamepadProcessDS4();
	void RdpGamepadProcessDS4Emulate();
};
