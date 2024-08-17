// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"

#include "RdpGamepadProcessor.h"

#include "ViGEmInterface.h"
#include <RdpGamepadProtocol.h>

RdpGamepadProcessor::RdpGamepadProcessor()
	: mRdpGamepadChannel(new RdpGamepad::RdpGamepadVirtualChannel())
	, mViGEmClient(std::make_shared<ViGEmClient>())
{}

RdpGamepadProcessor::~RdpGamepadProcessor()
{}

void RdpGamepadProcessor::Start(CONTROLLER_TYPE type)
{
	mType = type;
	mKeepRunning = true;
	mThread = std::thread(&RdpGamepadProcessor::Run, this);
}

void RdpGamepadProcessor::Stop()
{
	{
		std::unique_lock<std::recursive_mutex> lock{mMutex};
		mKeepRunning = false;
	}
	mThread.join();
}

void RdpGamepadProcessor::Run()
{
	static constexpr int PollFrequency = 16; // ms

	HANDLE TimerEvent;
	LARGE_INTEGER DueTime;
	DueTime.QuadPart = -1;

	TimerEvent = CreateWaitableTimerEx(NULL, NULL, 0, TIMER_ALL_ACCESS);
	SetWaitableTimer(TimerEvent, &DueTime, PollFrequency, NULL, NULL, false);

	std::unique_lock<std::recursive_mutex> lock{mMutex};
	while (mKeepRunning)
	{
		mMutex.unlock();
		const unsigned long WaitResult = WaitForSingleObject(TimerEvent, PollFrequency * 2);
		mMutex.lock();

		if (WaitResult == 0)
		{
			switch(mType)
			{
			case CONTROLLER_360:
				RdpGamepadProcess360();
				break;

			case CONTROLLER_360_EMU:
				RdpGamepadProcess360Emulate();
				break;

			case CONTROLLER_DS4:
				RdpGamepadProcessDS4();
				break;

			case CONTROLLER_DS4_EMU:
				RdpGamepadProcessDS4Emulate();
				break;
			}
		}
	}

	CancelWaitableTimer(TimerEvent);
	CloseHandle(TimerEvent);
	RdpGamepadTidy();
}

void RdpGamepadProcessor::RdpGamepadTidy()
{
	mViGEmTarget360 = nullptr;
	mViGEmTargetDS4 = nullptr;
	mRdpGamepadChannel->Close();
	mRdpGamepadConnected = false;
	mRdpGamepadPollTicks = 0;
}

void RdpGamepadProcessor::RdpGamepadProcess360()
{
	++mRdpGamepadPollTicks;

	// Try to open the channel if we don't have a channel open already
	if (!mRdpGamepadChannel->IsOpen())
	{
		// Only try to reconnect every few seconds instead of every tick as WTSVirtualChannelOpen can take a bit long.
		// I would probably be best if we called that outside of the critical section lock but for now this should
		// really make things much better.
		if (mRdpGamepadOpenRetry == 0)
		{
			if (!mRdpGamepadChannel->Open())
			{
				mRdpGamepadOpenRetry = 35; // Retry about every second
				RdpGamepadTidy();
				return;
			}
		}
		else
		{
			--mRdpGamepadOpenRetry;
			RdpGamepadTidy();
			return;
		}
	}

	//assert(mRdpGamepadChannel->IsOpen());
	if (!mRdpGamepadConnected)
	{
		//assert(mViGEmTarget360 == nullptr)
		mViGEmTarget360 = mViGEmClient->CreateControllerAs360();
		mRdpGamepadConnected = true;
		mErrorCode = S_OK;
	}

	// Request controller state and update vibration
	if (!mRdpGamepadChannel->Send(RdpGamepad::RdpGetStateRequest::MakeRequest(0)))
	{
		RdpGamepadTidy();
		return;
	}

	XINPUT_VIBRATION PendingVibes;
	if (mViGEmTarget360->GetVibration(PendingVibes))
	{
		if (!mRdpGamepadChannel->Send(RdpGamepad::RdpSetStateRequest::MakeRequest(0, PendingVibes)))
		{
			RdpGamepadTidy();
			return;
		}
	}

	// Read all the pending messages
	RdpGamepad::RdpProtocolPacket packet;
	while (mRdpGamepadChannel->Receive(&packet))
	{
		// Handle controller state
		if (packet.mHeader.mMessageType == RdpGamepad::RdpMessageType::GetStateResponse)
		{
			if (packet.mGetStateResponse.mUserIndex == 0)
			{
				if (packet.mGetStateResponse.mResult == 0)
				{
					mViGEmTarget360->SetGamepadState(packet.mGetStateResponse.mState.Gamepad);
				}
				else
				{
					mViGEmTarget360->SetGamepadState(XINPUT_GAMEPAD{0});
					mErrorCode = packet.mGetStateResponse.mResult;
				}
				mLastGetStateResponseTicks = mRdpGamepadPollTicks;
			}
		}
	}

	// Remove stale controller data
	if (mRdpGamepadPollTicks < mLastGetStateResponseTicks || (mRdpGamepadPollTicks - mLastGetStateResponseTicks) > 120) // Timeout in about 2 seconds
	{
		mViGEmTarget360->SetGamepadState(XINPUT_GAMEPAD{0});
	}

	// Check connection state
	if (!mRdpGamepadChannel->IsOpen())
	{
		RdpGamepadTidy();
	}
}

void RdpGamepadProcessor::RdpGamepadProcess360Emulate()
{
	++mRdpGamepadPollTicks;

	// Try to open the channel if we don't have a channel open already
	if (!mRdpGamepadChannel->IsOpen())
	{
		// Only try to reconnect every few seconds instead of every tick as WTSVirtualChannelOpen can take a bit long.
		// I would probably be best if we called that outside of the critical section lock but for now this should
		// really make things much better.
		if (mRdpGamepadOpenRetry == 0)
		{
			if (!mRdpGamepadChannel->Open())
			{
				mRdpGamepadOpenRetry = 35; // Retry about every second
				RdpGamepadTidy();
				return;
			}
		}
		else
		{
			--mRdpGamepadOpenRetry;
			RdpGamepadTidy();
			return;
		}
	}

	//assert(mRdpGamepadChannel->IsOpen());
	if (!mRdpGamepadConnected)
	{
		//assert(mViGEmTarget360 == nullptr)
		mViGEmTarget360 = mViGEmClient->CreateControllerAs360();
		mRdpGamepadConnected = true;
		mErrorCode = S_OK;
	}

	// Request controller state and update vibration
	if (!mRdpGamepadChannel->Send(RdpGamepad::RdpGetStateRequestDS4::MakeRequest(0)))
	{
		RdpGamepadTidy();
		return;
	}

	PadVibrationParam PendingVibesDS4;
	if (mViGEmTarget360->GetVibration(PendingVibesDS4))
	{
		if (!mRdpGamepadChannel->Send(RdpGamepad::RdpSetStateRequestDS4::MakeRequest(0, PendingVibesDS4)))
		{
			RdpGamepadTidy();
			return;
		}
	}

	// Read all the pending messages
	RdpGamepad::RdpProtocolPacket packet;
	while (mRdpGamepadChannel->Receive(&packet))
	{
		// Handle controller state
		if (packet.mHeader.mMessageType == RdpGamepad::RdpMessageType::GetStateResponseDS4)
		{
			if (packet.mGetStateResponseDS4.mResult == 0)
			{
				mViGEmTarget360->SetGamepadState(packet.mGetStateResponseDS4.mState);
			}
			else
			{
				mViGEmTarget360->SetGamepadState(XINPUT_GAMEPAD{0});
				mErrorCode = packet.mGetStateResponseDS4.mResult;
			}
			mLastGetStateResponseTicks = mRdpGamepadPollTicks;
		}
	}

	// Remove stale controller data
	if (mRdpGamepadPollTicks < mLastGetStateResponseTicks || (mRdpGamepadPollTicks - mLastGetStateResponseTicks) > 120) // Timeout in about 2 seconds
	{
		mViGEmTarget360->SetGamepadState(XINPUT_GAMEPAD{0});
	}

	// Check connection state
	if (!mRdpGamepadChannel->IsOpen())
	{
		RdpGamepadTidy();
	}
}

void RdpGamepadProcessor::RdpGamepadProcessDS4()
{
	++mRdpGamepadPollTicks;

	// Try to open the channel if we don't have a channel open already
	if (!mRdpGamepadChannel->IsOpen())
	{
		// Only try to reconnect every few seconds instead of every tick as WTSVirtualChannelOpen can take a bit long.
		// I would probably be best if we called that outside of the critical section lock but for now this should
		// really make things much better.
		if (mRdpGamepadOpenRetry == 0)
		{
			if (!mRdpGamepadChannel->Open())
			{
				mRdpGamepadOpenRetry = 35; // Retry about every second
				RdpGamepadTidy();
				return;
			}
		}
		else
		{
			--mRdpGamepadOpenRetry;
			RdpGamepadTidy();
			return;
		}
	}

	//assert(mRdpGamepadChannel->IsOpen());
	if (!mRdpGamepadConnected)
	{
		//assert(mViGEmTargetDS4 == nullptr)
		mViGEmTargetDS4 = mViGEmClient->CreateControllerAsDS4();
		mRdpGamepadConnected = true;
		mErrorCode = S_OK;
	}

	// Request controller state and update vibration
	if (!mRdpGamepadChannel->Send(RdpGamepad::RdpGetStateRequestDS4::MakeRequest(0)))
	{
		RdpGamepadTidy();
		return;
	}

	PadVibrationParam PendingVibesDS4;
	if (mViGEmTargetDS4->GetVibration(PendingVibesDS4))
	{
		if (!mRdpGamepadChannel->Send(RdpGamepad::RdpSetStateRequestDS4::MakeRequest(0, PendingVibesDS4)))
		{
			RdpGamepadTidy();
			return;
		}
	}

	// Read all the pending messages
	RdpGamepad::RdpProtocolPacket packet;
	while (mRdpGamepadChannel->Receive(&packet))
	{
		// Handle controller state
		if (packet.mHeader.mMessageType == RdpGamepad::RdpMessageType::GetStateResponseDS4)
		{
			if (packet.mGetStateResponseDS4.mResult == 0)
			{
				mViGEmTargetDS4->SetGamepadState(packet.mGetStateResponseDS4.mState);
			}
			else
			{
				mViGEmTargetDS4->SetGamepadState(XINPUT_GAMEPAD{0});
				mErrorCode = packet.mGetStateResponseDS4.mResult;
			}
			mLastGetStateResponseTicks = mRdpGamepadPollTicks;
		}
	}

	// Remove stale controller data
	if (mRdpGamepadPollTicks < mLastGetStateResponseTicks || (mRdpGamepadPollTicks - mLastGetStateResponseTicks) > 120) // Timeout in about 2 seconds
	{
		mViGEmTargetDS4->SetGamepadState(XINPUT_GAMEPAD{0});
	}

	// Check connection state
	if (!mRdpGamepadChannel->IsOpen())
	{
		RdpGamepadTidy();
	}
}

void RdpGamepadProcessor::RdpGamepadProcessDS4Emulate()
{
	++mRdpGamepadPollTicks;

	// Try to open the channel if we don't have a channel open already
	if (!mRdpGamepadChannel->IsOpen())
	{
		// Only try to reconnect every few seconds instead of every tick as WTSVirtualChannelOpen can take a bit long.
		// I would probably be best if we called that outside of the critical section lock but for now this should
		// really make things much better.
		if (mRdpGamepadOpenRetry == 0)
		{
			if (!mRdpGamepadChannel->Open())
			{
				mRdpGamepadOpenRetry = 35; // Retry about every second
				RdpGamepadTidy();
				return;
			}
		}
		else
		{
			--mRdpGamepadOpenRetry;
			RdpGamepadTidy();
			return;
		}
	}

	//assert(mRdpGamepadChannel->IsOpen());
	if (!mRdpGamepadConnected)
	{
		//assert(mViGEmTargetDS4 == nullptr)
		mViGEmTargetDS4 = mViGEmClient->CreateControllerAsDS4();
		mRdpGamepadConnected = true;
		mErrorCode = S_OK;
	}

	// Request controller state and update vibration
	if (!mRdpGamepadChannel->Send(RdpGamepad::RdpGetStateRequest::MakeRequest(0)))
	{
		RdpGamepadTidy();
		return;
	}

	XINPUT_VIBRATION  PendingVibes360;
	if (mViGEmTargetDS4->GetVibration(PendingVibes360))
	{
		if (!mRdpGamepadChannel->Send(RdpGamepad::RdpSetStateRequest::MakeRequest(0, PendingVibes360)))
		{
			RdpGamepadTidy();
			return;
		}
	}

	// Read all the pending messages
	RdpGamepad::RdpProtocolPacket packet;
	while (mRdpGamepadChannel->Receive(&packet))
	{
		// Handle controller state
		if (packet.mHeader.mMessageType == RdpGamepad::RdpMessageType::GetStateResponse)
		{
			if (packet.mGetStateResponse.mUserIndex == 0)
			{
				if (packet.mGetStateResponse.mResult == 0)
				{
					mViGEmTargetDS4->SetGamepadState(packet.mGetStateResponse.mState.Gamepad);
				}
				else
				{
					mViGEmTargetDS4->SetGamepadState(XINPUT_GAMEPAD{0});
					mErrorCode = packet.mGetStateResponse.mResult;
				}
				mLastGetStateResponseTicks = mRdpGamepadPollTicks;
			}
		}
	}

	// Remove stale controller data
	if (mRdpGamepadPollTicks < mLastGetStateResponseTicks || (mRdpGamepadPollTicks - mLastGetStateResponseTicks) > 120) // Timeout in about 2 seconds
	{
		mViGEmTargetDS4->SetGamepadState(XINPUT_GAMEPAD{0});
	}

	// Check connection state
	if (!mRdpGamepadChannel->IsOpen())
	{
		RdpGamepadTidy();
	}
}