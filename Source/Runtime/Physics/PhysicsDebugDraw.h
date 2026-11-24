#pragma once

class UWorld;
class FLineBatcher;

// 월드 물리 씬의 실제 바디·관절을 선으로 그린다 (UE Show > Collision / Constraints).
// 바디 색: 시뮬레이션(주황) · 잠든 시뮬레이션(회색) · 키네마틱(파랑) · 정적(초록).
// 관절: 두 바디 쪽 프레임의 축(X 빨강=Twist, Y 초록, Z 파랑)과 둘을 잇는 선. 부서진 관절은 자홍.
void DrawPhysicsDebug(UWorld* World, FLineBatcher* Lines, bool bDrawBodies, bool bDrawConstraints);
