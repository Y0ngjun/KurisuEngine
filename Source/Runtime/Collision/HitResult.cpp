#include "EnginePCH.h"
#include "Components/PrimitiveComponent.h"
AActor* FHitResult::GetActor() const
{
	return HitComponent ? HitComponent->GetOwner() : nullptr;
}

AActor* FOverlapResult::GetActor() const
{
	return Component ? Component->GetOwner() : nullptr;
}
