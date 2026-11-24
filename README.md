# KurisuEngine

C++20 / Direct3D 11 기반의 Windows용 게임 엔진과 에디터입니다.

## 요구 사항

- Windows 10/11 x64
- Visual Studio 2026 (C++ 데스크톱 개발 워크로드)

## 빌드

```powershell
.\SlnGenerator.bat
```

premake로 `Kurisu.slnx`를 만듭니다. 솔루션을 열고 `KurisuEditor`(시작 프로젝트)를 빌드·실행하면 됩니다.

## 프로젝트 구성

| 프로젝트 | 설명 |
| --- | --- |
| `KurisuEngine` | 런타임 엔진 (정적 라이브러리) |
| `KurisuEditor` | ImGui 기반 에디터 |
| `KurisuGame` | 패키징된 게임 실행 파일 |
| `KurisuHeaderTool` | 리플렉션 코드 생성 도구 |
| `Shaders` | HLSL 셰이더 |

## 폴더 구조

```
Source/Runtime      엔진 코어 (렌더링, 애니메이션, 물리, UI, 스크립트 등)
Source/Editor       에디터 (레벨/에셋/애니메이션/파티클/물리 에셋 편집기)
Source/Game         게임 진입점
Source/Programs     빌드 도구 (KurisuHeaderTool)
Source/ThirdParty   외부 라이브러리
EngineResources     셰이더, 폰트, 아이콘, 기본 에셋
Setup/Build         premake 스크립트, PhysX 설치 스크립트
Setup/Packaging     게임·에디터 패키징 스크립트
```

## 주요 기능

- 타일 기반 라이트 컬링, 그림자, 데칼, 높이 안개, 톤매핑·FXAA
- 스켈레탈 애니메이션, 애니메이션 블루프린트, 블렌드 스페이스, GPU 스키닝
- PhysX 4.1 물리, NvCloth 천 시뮬레이션
- 파티클 시스템, UI, 오디오(miniaudio)
- Lua 스크립팅(sol2)
- FBX 임포트

## 패키징

```powershell
.\Setup\Packaging\Package.bat Release path\to\Game.kproject
```

## 외부 라이브러리

ImGui, ImGui Node Editor, PhysX, NvCloth, FBX SDK, Lua, sol2, nlohmann/json, miniaudio, stb, DDSLoader
