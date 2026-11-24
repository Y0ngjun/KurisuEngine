#include "EnginePCH.h"
#include "BillboardActor.h"

ABillboardActor::ABillboardActor()
{
	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
}

ABillboardActor::~ABillboardActor()
{
}
