// 물리 에셋 에디터의 반투명 바디.
// 구 메시 하나로 구·캡슐을, 정육면체 메시 하나로 박스를 그린다.
// 캡슐은 구의 위쪽 절반을 +HalfLength, 아래쪽 절반을 -HalfLength 만큼 밀어 가운데를 원기둥으로 만든다.
#pragma pack_matrix(row_major)
cbuffer constants : register(b0)
{
	matrix World;          // 스케일 없는 형상 프레임
	matrix ViewProjection;
	float4 Color;          // a = 불투명도
	float3 ShapeScale;     // 메시 → 형상 크기
	float HalfLength;      // 캡슐 원기둥 반길이 (구·박스는 0)
	float3 CameraPosition;
	float Padding;
};

struct VS_INPUT
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float4 tangent : TANGENT;
	float2 uv : TEXCOORD;
	float4 color : COLOR;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float3 worldPosition : TEXCOORD0;
	float3 worldNormal : TEXCOORD1;
};

PS_INPUT mainVS(VS_INPUT input)
{
	float3 Local = input.position * ShapeScale;
	Local.z += input.position.z >= 0.0f ? HalfLength : -HalfLength;

	// 늘린 형상의 법선 = 원래 법선 / 스케일
	float3 Normal = input.normal / max(ShapeScale, 1e-4f);

	PS_INPUT output;
	float4 WorldPosition = mul(float4(Local, 1.0f), World);
	output.worldPosition = WorldPosition.xyz;
	output.worldNormal = mul(Normal, (float3x3)World);
	output.position = mul(WorldPosition, ViewProjection);
	return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
	// 카메라를 향한 면은 밝게, 옆면은 어둡게 해서 입체감만 준다 (라이트는 쓰지 않는다).
	float Facing = 1.0f;
	if (dot(input.worldNormal, input.worldNormal) > 1e-8f)
		Facing = abs(dot(normalize(input.worldNormal), normalize(CameraPosition - input.worldPosition)));
	return float4(Color.rgb * (0.45f + 0.55f * Facing), Color.a);
}
