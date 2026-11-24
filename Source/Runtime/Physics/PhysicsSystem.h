#pragma once

#include <memory>

namespace physx
{
	class PxPhysics;
}

// 엔진 루프가 소유하는 프로세스 단위 SDK. 월드별 PxScene은 이 객체보다 먼저 해제한다.
// 초기화/종료는 엔진 메인 스레드에서만 호출한다.
class FPhysicsSystem
{
public:
	FPhysicsSystem();
	~FPhysicsSystem();
	FPhysicsSystem(const FPhysicsSystem&) = delete;
	FPhysicsSystem& operator=(const FPhysicsSystem&) = delete;

	// 중복 호출은 성공한 SDK를 그대로 사용한다. 실패하면 부분 생성한 자원도 정리한다.
	bool Init();
	// 여러 번 호출하거나 초기화에 실패한 뒤 호출해도 안전하다.
	void Shutdown();
	bool IsInitialized() const;
	// 초기화 전/실패 후/종료 후에는 nullptr. 소유권은 넘기지 않는다.
	physx::PxPhysics* GetPhysics() const;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};
