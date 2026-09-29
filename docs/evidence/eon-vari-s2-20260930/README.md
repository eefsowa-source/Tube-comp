# EON-Vari mu DSP — S2 vari-mu coupling 증거 (2026-09-30)

S2(GR → 튜브 바이어스 커플링)가 반영된 설치 VST3의 게이트 증거입니다.
REAPER는 제외했고 EON `soundcraft`/`eonqc-host` 경로를 사용했습니다.

## 1. 식별자

- binary SHA-256 (VST3): `bc067c95d7c7fc5c4e80a1b4fb85a8be2cfa00474e5fdce2d90e0cb5ae4bae94`
- binary SHA-256 (AU): `7211b3f79d791027aa7d24f99f6bc398711e32e61c08465cb9424cb54774924e`
- 호스트: `eonqc-host 0.5.0; JUCE v9.0.1`, host SHA-256 `e9c368a865225852437eeabd0e2b2bc5ef33c1234d1c3706c1e36c49053f6fc9`
- soundcraft 0.2.0 (git `ee5f9ae`), measurement algorithm `0.3.0`
- contract / run_id: `cbabb512740baa6bd23b3156467988d8fff2b9e330242c2a3788d369e557543e`
- 매니페스트: `eon-vari-smoke.json` (S2 이전과 동일, 6 시나리오)
- 48 kHz, 512 frames, stereo, effect. VST3/AU 모두 codesign `--deep --strict` 통과.

## 2. 게이트 결과

- `ctest --test-dir build --output-on-failure` → **9/9 통과** (ProcessorTest 포함, 3.5 s)
- `eonqc`/`soundcraft` 스모크 → **6/6 통과** (`|x| <= 1.0` 및 유한값 검증 포함)

## 3. 커플링 효과 (호스트 측정, S2 이전 → 이후)

GR이 클수록 2차 고조파가 증가합니다(프로그램 의존 왜곡).

| 시나리오 | 입력 | peak (dBFS) | H2 (dBc) | RMS delta |
|---|---|---|---|---|
| nominal_feedforward_1k | -18 | -6.47 → -6.60 | -32.67 → -32.37 | +0.01 dB |
| nominal_feedback_1k | -18 | -6.40 → -6.47 | -32.59 → -32.35 | +0.01 dB |
| tc6_feedback_1k | -18 | -6.48 → -6.72 | -32.70 → -32.38 | +0.01 dB |
| latver_feedback_1k | -18 | -6.41 → -6.58 | -33.40 → -32.55 | +0.04 dB |
| hot_feedforward_1k | -12 | -1.11 → -1.53 | -29.83 → -26.96 | +0.17 dB |

RMS는 전 시나리오에서 0.17 dB 이내로 유지됩니다 — 소출력 트림이 컴프레서의
레벨 법칙을 보존하고, 커플링은 고조파 성격만 바꿉니다. 스모크의 계약은
`channel_peak_delta_db`, `non_finite`, sample rate, duration만 검사하므로
고조파 변화는 게이트를 깨지 않습니다.

렌더 비용(`realtime_factor`, 진단값)은 7.0x → 6.7x로 약 4% 증가해 S2 예산
(+5% 이내) 안에 들어옵니다. 시나리오 6개 모두 같은 방향으로 일관됩니다.
같은 매니페스트를 두 번 실행한 출력 해시가 동일해 렌더가 결정론적입니다.

## 4. 단위 회귀 (ProcessorTest, 케이스 10·11)

베어 트라이오드 스테이지에서 바이어스를 직접 스윕한 값입니다(drive 6 dB,
-6 dBFS 1 kHz).

| control bias | H2 (dBc) | vs neutral | 출력 레벨 오차 |
|---|---|---|---|
| 0.00 V | -18.5 | — | — |
| 0.45 V | -16.3 | +2.2 dB | -0.00 dB |
| 0.90 V | -14.1 | +4.4 dB | +0.03 dB |
| 1.35 V | -12.1 | +6.4 dB | +0.05 dB |
| 1.80 V | -10.2 | +8.3 dB | +0.00 dB |

보상 계수는 이 스윕에서 피팅했습니다(`1.36·δ + 0.63·δ²` dB). 케이스 11은
엔드투엔드 배선을 확인합니다: GR이 0이면 바이어스 0, GR이 커지면 더 차갑게.
케이스 12는 안정성 스트레스입니다: drive 24 dB, ratio 20:1, threshold -60 dB,
8x, feedback에서 loud/silent 버스트 400블록 → peak 4.99, non-finite 0.
