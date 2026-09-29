# EON-Vari mu DSP — S3 출력 트랜스포머 증거 (2026-09-30)

계획 8.2 S3의 선형 근사 단계다. Fairchild 670 계열의 입출력 철심이 음색의
큰 일부라는 점을, 측정 가능한 스펙트럼 차이로 확인하는 것이 이 단계의
목적이다. 비선형 히스테리시스 인덕터는 이 게이트를 통과한 뒤에만 검토한다.

## 1. 식별자

- binary SHA-256 (VST3): `947df1f663c67bd6c6f3bf1b280b2cf448fa98a19012882001ce9262cb400608`
- 호스트: `eonqc-host 0.5.0; JUCE v9.0.1`, host SHA-256 `e9c368a865225852437eeabd0e2b2bc5ef33c1234d1c3706c1e36c49053f6fc9`
- soundcraft 0.2.0 (git `ee5f9ae`), measurement algorithm `0.3.0`
- contract / run_id: `abcb0a62783bbf83a7709c78c9a2ebe1306b64d87ccf3d4bd3f625c5fe9d18c3`
- 실행 ID: `d18ebd690e9741d5904d7a3eb2e8a606`
- 매니페스트: 6 시나리오, 48 kHz, 512 frames, stereo, effect
- OS: macOS 15.7.9 (24G830), arm64

## 2. 게이트 결과

- CTest: 9/9 통과 (1.8 s)
- soundcraft 6시나리오: 6/6 통과, `regressed: false` (직전 리포트 대비)
- auval `-v aufx Eonc Eonn`: PASS
- VST3 pluginval: 미실행. Aqua 세션 부재 (직전 UI 단계와 동일한 제약)

## 3. 측정 결과

wdf-4x, drive 12, -12 dBFS. `Tools/sound_baseline.cpp`의 S3 표.
RMS/H3/alias 모두 절대값과 델타를 함께 출력한다.

40 Hz: rms 1.36 / 1.79 / **+0.43**, H3 -33.1 / -30.0 / +3.0, alias -40.8 / -40.8 / 0.0

1 kHz: rms 1.68 / 1.31 / **-0.37**, H3 -32.8 / -29.6 / +3.2, alias -40.4 / -40.4 / 0.0

15 kHz: rms 1.62 / 1.14 / **-0.48**, H3 -118.3 / -124.5 / -6.2, alias -106.4 / -82.5 / +23.9

CPU (실시간 대비 %, 48 kHz / 512 블록), off / on / delta:

    1x   1.31 /  1.51 / +0.20
    2x   3.54 /  3.82 / +0.27
    4x   6.30 /  6.96 / +0.67
    8x  12.46 / 13.10 / +0.65

판단: 저역 +0.43 dB, 중역 -0.37 dB, 고역 -0.48 dB로 저역 무게와 상단
롤오프가 생겼다. H3는 +3 dB 상승해 철심 색이 실렸고, 1 kHz와 40 Hz의
alias는 변화가 없다. 8x에서 CPU는 13.10%로 S1 목표(12% 이하)를 1.1 pp
넘지만, 이 오버헤드는 비선형 인덕터가 아니라 셸프 두 개와 비선형성 한
번의 비용이다. 오버헤드는 4x에서 최대(+0.67 pp)이고 8x에서는 +0.65 pp로,
오버샘플 레벨이 아니라 샘플당 고정 비용이 지배적이라는 점을 보여준다.

### 정정 기록

이 표를 처음 만들었을 때 `names[factor - 1]` 인덱스가 잘못되어 2x를 "1x"
로, 4x를 "2x"로 라벨했다. 동시에 8x 행이 측정 대상에서 빠져 있었다. 위
수치는 라벨과 범위를 고친 뒤의 재측정값이다. 이전 실행의 "1x 3.46 /
4x 11.90"은 실제로는 2x와 8x 값이었고, 8x 오버헤드가 +0.96 pp가 아니라
+0.65 pp였다. 스펙트럼 델타(3절)는 라벨과 무관하므로 영향이 없다.

## 4. 구현 중 발견해 바로잡은 것

**1차 one-pole의 샘플레이트 의존성.** `a = 1 - exp(-2*pi*f/fs)`는 48 kHz와
192 kHz에서 -3 dB 지점이 다르다. 22 kHz 코너 하나로 셋업한 결과 15 kHz가
48 kHz에서 -0.2 dB, 192 kHz에서 -1.6 dB 죽었다. Hz로 지정되는 셸프는
JUCE의 rate-correct 바이쿼드로 교체했고, 192 kHz에서도 15 kHz -0.39 dB로
목표를 지켰다.

**DC blocker 제거.** 스테이지에 10 ms DC blocker를 넣었더니 40 Hz에서
-0.64 dB를 먹어 저역 셸프와 상쇄됐다. 튜브 스테이지 뒤에서 동작하고
비선형성이 홀함수라 DC를 만들지 않으므로 DC blocker는 필요 없다.

**비선형 함수 선택.** tanh는 H3가 -66 dBc였지만 H5가 -107 dBc, H7이
-148 dBc였다. 4x에서 15 kHz 프로브의 H7(105 kHz)이 96 kHz Nyquist를
넘어 디시메이션 시 9 kHz로 폴딩되어 alias가 +28 dB 올랐다. 경도 클리프된
3차 함수는 반대로 H3조차 -300 dBc라 음색까지 사라졌다. 최종
`x/sqrt(1+(x/k)^2)` 형태(k=1.5, mix=0.15)는 H3 -50 dBc, H5 -88 dBc로,
3차 고조파만 남기고 폴딩 원인을 제거했다.

**knee 위치.** k=0.6은 프로그램 레벨 안쪽에 무릎이 있어 1 kHz에서
-1.12 dB 레벨 회귀를 만들었다. k=1.5로 올려 프로그램 구간 0.1 dB 이내로
옮겼다.

## 5. 남은 게이트

- 기본값은 **Off**다. smoke 매니페스트가 기존 상태를 그대로 재현하도록.
- 레벨 매칭 블라인드 청취가 아직 없다. 이것이 이 단계의 마지막 관문이며,
  여기서 이득이 확인되어야 비선형 히스테리시스 인덕터(WDF 루트 추가)로
  진행 여부를 결정한다.
- 호스트 UI 상호작용, VST3 pluginval은 UI 단계와 동일한 제약 하에 남아 있다.

## 6. 재현

    cmake --build build --parallel
    ctest --test-dir build --output-on-failure
    ./build/eonchild_SoundBaseline            # S3 표 포함 전체 출력
    ./build/eonchild_EditorSnapshot /tmp/eon-ui-iron-1180.png 1180 820
    /Users/sungha/Documents/Codex/soundcraft/bin/soundcraft run \
        build/eon-vari-smoke.json --out build/eon-vari-smoke-s3-report.json --json
    auval -v aufx Eonc Eonn
