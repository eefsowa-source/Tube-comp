# EON-Vari mu DSP — UI 670 레퍼런스 개편 증거 (2026-09-30)

Fairchild 670 / UAD Fairchild 레퍼런스 비주얼 문법에 맞춘 에디터 개편의 증거입니다.
상업 제품의 페이스플레이트·로고·아트워크는 복제하지 않고, 레이어 문법(채널 스트립
2단 구성, 패널 인쇄 스케일, 하단 유틸리티 밴드, 크림 네임플레이트)만 참조했습니다.
REAPER는 제외했고 EON `soundcraft`/`eonqc-host` 경로를 사용했습니다.

## 1. 식별자

- binary SHA-256 (VST3, 빌드/설치 동일): `101bdc017828cc6d7f73735a983506331178bf13323017864dc2bb2c5ee5c3d4`
- binary SHA-256 (AU): `2a85088d4a09a95f6314d6a1738d82f9d894691d62bec62031a31358750e29fe`
- 호스트: `eonqc-host 0.5.0; JUCE v9.0.1`, host SHA-256 `e9c368a865225852437eeabd0e2b2bc5ef33c1234d1c3706c1e36c49053f6fc9`
- soundcraft 0.2.0 (git `ee5f9ae`), measurement algorithm `0.3.0`
- contract / run_id: `aa4ace611be14880fa7985395027f2f72c64365805c106db5f01a898f2c0ed8b`
- 실행 ID: `7f501d9111aa439b8777d34a6d7948b7`
- 매니페스트: 6 시나리오, 48 kHz, 512 frames, stereo, effect
- 코드사인: ad-hoc, `codesign --verify --deep --strict` 통과 (양쪽 번들)
- OS: macOS 15.7.9 (24G830), arm64

## 2. 게이트 결과

| 게이트 | 명령 | 결과 |
|---|---|---|
| 단위/회귀 | `ctest --test-dir build --output-on-failure` | **9/9 통과** (1.8 s) |
| 오디오 품질 | `soundcraft run build/eon-vari-smoke.json` | **6/6 통과**, host pass, 회귀 없음 |
| AU 포맷 | `auval -v aufx Eonc Eonn` | **PASS** (`AU VALIDATION SUCCEEDED`) |
| VST3 포맷 | `pluginval --strictness-level 10` | **미실행 — 아래 §5 참조** |

회귀 비교(`soundcraft compare`, 이전 UI 리포트 대비): `regressed: false`,
old/new status 모두 `pass`. 변화 메트릭은 `render_seconds`, `realtime_factor` 두
종뿐이며 이는 CPU 타이밍 측정치다. THD/H2/H3·별칭 등 스펙트럼 수치는 변하지 않았다.

## 3. UI 변경 내용

- `Source/UI/TubeCompLookAndFeel.{h,cpp}`: 300° 스윕(하단 갭) 패널 인쇄 눈금,
  큰 THRESHOLD 링 다이얼과 0–5 번호 페ンス, 크림 배트 핸들 BYPASS 토글,
  평면 검은 노브 + 흰색 인덱스 포인터. 미사용 색상 상수 4개 제거.
- `Source/PluginEditor.cpp`: 그라파이트 유틸리티 패널, 좌/우 채널 스트립 2단,
  하단 유틸리티 밴드(트림 4 + 모드 스위치 6 + GR 미터), 크림 네임플레이트.
  채널 필드와 유틸리티 밴드 사이, 두 채널 행 사이에 머신 가공 이음새(seam)를
  헤어라인 홈으로 추가해 하드웨어 판넬의 섹션 분리를 표현.
- `Source/UI/VUMeterComponent.{h,cpp}`: VU 눈금(−20…+3)과 비선형 GR 눈금(0/1/2/4/8/20)을
  하나의 크림 다이얼에 인쇄. 펀치 상위 바이어스를 크림 다이얼 색으로 처리.
- 기존 툴바, A/B, undo/redo, 미터, APVTS 동작은 유지.

미니멈 980×800과 기본 1180×820 두 크기에서 클리핑 없이 렌더링됨을 확인했다
(`editor-1180x820.png`, `editor-980x800.png`).

## 4. 스냅샷 재현

오프스크린 렌더는 실제 에디터를 그대로 페인팅한다(더미 호스트 아님).

```bash
./build/eonchild_EditorSnapshot /tmp/eon-ui-1180.png 1180 820
./build/eonchild_EditorSnapshot /tmp/eon-ui-980.png 980 800
```

## 5. 미해결 / 다음 게이트

- **VST3 pluginval 미실행.** 이 에이전트 실행 환경은 `/dev/console` 소유자가 root라
  Aqua 세션이 없다. pluginval은 AppKit 런 루프에서 유휴 상태로 대기하며 플러그인
  로드 전에 멈춘다(`pluginval-stall-sample.txt` 참조). GUI 로그인 세션에서 재실행 필요.
- **호스트 UI 상호작용 미검증.** 오프스크린 스냅샷은 레이아웃·렌더만 증명한다.
  드래그/호버/리사이즈 동작은 Ableton Live에서 별도로 확인해야 한다.
- pluginval 로그가 비어 있는 점도 같은 원인(Aqua 세션 부재)으로 봅니다.
- 청취는 레벨 매칭 블라인드로 아직 수행하지 않았다.

UI 수락 후 S3(트랜스포머) 작업으로 진행한다.
