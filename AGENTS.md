# Tube comp / eonchild

- `CMakeLists.txt`가 JUCE 경로와 CTest 대상을 정의한다. DSP는 `Source/DSP/`, 플러그인·UI는 `Source/`, 회귀 검사는 `Tests/`에 있다.
- CMake가 `/Users/sungha/JUCE`를 참조하므로 설정 전에 경로를 확인한다. DSP 변경 후 해당 테스트를 빌드하고 `ctest --test-dir <빌드 디렉터리> --output-on-failure`를 실행한다.
- `scripts/package_macos.sh`와 `Packaging/`은 빌드와 별도 배포 경로다. 설치 바이너리, 호스트, UI와 청취 결과를 구분한다.
- `FLOP Labs/Technocore/technocore-chat`은 자체 저장소와 `AGENTS.md`가 있는 독립 작업이다.
