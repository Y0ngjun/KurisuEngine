#pragma once

#include "Input/InputSystem.h"

// PIE and standalone play must forward the same keys, including releases.
// 게임에는 글자(A~Z)·숫자(0~9)·Space·Shift를 모두 넘긴다 (이동 키만 넘기면 F 같은 게임 조작이 막힌다).
// Escape와 기능 키(F1~F12)는 각 호스트의 플레이 세션 조작(F8 빙의 해제, Shift+F1 등)이 따로 처리한다.
template <typename TReceiver>
void DispatchGameKeyboardInput(TReceiver&& Receive)
{
	auto Forward = [&](EKeyCode Key)
	{
		if (FInputSystem::IsKeyPressed(Key))
			Receive(static_cast<int32>(Key), true);
		if (FInputSystem::IsKeyReleased(Key))
			Receive(static_cast<int32>(Key), false);
	};
	for (int32 Key = static_cast<int32>(EKeyCode::A); Key <= static_cast<int32>(EKeyCode::Z); ++Key)
		Forward(static_cast<EKeyCode>(Key));
	for (int32 Key = static_cast<int32>(EKeyCode::Num0); Key <= static_cast<int32>(EKeyCode::Num9); ++Key)
		Forward(static_cast<EKeyCode>(Key));
	Forward(EKeyCode::Space);
	Forward(EKeyCode::Shift);
}
