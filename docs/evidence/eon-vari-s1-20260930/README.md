# EON-Vari mu DSP — S1 CPU 재확보 증거 (2026-09-30)

S1(8x WDF 뉴턴 솔버 비용 절감)이 반영된 설치 VST3/AU의 게이트 증거입니다.
REAPER는 제외했고 EON `soundcraft`/`eonqc-host` 경로를 사용했습니다.

## 1. 식별자

- binary SHA-256 (VST3): `76e4002ce120c41d558cd3ea0a37756ad5f7acb60b523d098cc47863db12bf33`
- binary SHA-256 (AU): `a6b60bee69647d4345721571fdc3432abf2dbe9b365843bbd1232777b9227ec3`
- 호스트: `eonqc-host 0.5.0; JUCE v9.0.1`, host SHA-256 `e9c368a865225852437eeabd0e2b2bc5ef33c1234d1c3706c1e36c49053f6fc9`
- soundcraft 0.2.0 (git `ee5f9ae`), measurement algorithm `0.3.0`
- contract / run_id: `d43fc4353adabe643c4d0fab16853b0de8a3ef4bc2d98dc541ba380eb078708e`
- 매니페스트: `eon-vari-smoke.json` (6 시나리오), 48 kHz, 512 frames, stereo, effect

## 2. 게이트 결과

- `ctest --test-dir build --output-on-failure` → **9/9 통과** (ProcessorTest 포함, 2.1 s)
- `soundcraft run build/eon-vari-smoke.json` → **6/6 통과** (host pass)

## 3. CPU 비용 (sound_baseline, 48 kHz, 512 블록, 실시간 대비 %)

| 구성 | S1 이전 | S1 이후 | 감소 |
|---|---|---|---|
| wdf-1x | 2.45 | 1.30 | 1.15 pp |
| wdf-2x | 5.19 | 3.45 | 1.74 pp |
| wdf-4x | 9.93 | 6.26 | 3.67 pp |
| wdf-8x | 19.04 | 11.88 | 7.16 pp |

목표(8x ≤12%)를 달성했습니다. fast 경로는 2.04→2.08%로 불변(측정 노이즈 수준).

## 4. 구현 내용

- `KorenTriode.h`: softplus+logistic이 공유하는 exp 1회로 축약, `pow(e1, 0.4)` 재사용.
- `TriodeStage.cpp`: 뉴턴 루프를 `PlateSolve{vpk, ip}` 구조체로 재구성해
  평가-후-루프 + 최종 스텝 전 종료로 `koren.ip`를 재사용(추가 전류 평가 1회 제거).
- 선형 외삽 웜 스타트: `warm = 2·Vpk − VpkPrev`(`plateVpkPrev` 상태 추가).
  평균 뉴턴 반복 2.80 → 2.19.
- 허용오차는 1e-6 유지(1e-7과 비트 동일, −311 dBFS). 계획 §8.2의 a항
  (tol 1e-6→1e-5) 대신 반복 경로 최적화로 목표를 달성했습니다.

## 5. 품질 게이트와 널 비교

- tone/alias 테이블(THD, H2, H3, rms, crest, alias dBc, 레벨 매칭, 정적 곡선)은
  S1 이전과 출력 해상도(0.1 dB)에서 **동일**.
- 널 비교(240k 샘플, 48 kHz): max `|a−b|` **−95.2 dBFS**(샘플 78559),
  rms −111.9 dBFS(신호 대비 116.6 dB 아래).

계획의 널 기준(−100 dBFS)은 peak 기준 4.8 dB 미달입니다. 원인 분석:
솔버 반복 경로를 조금만 바꿔도(구조 변경, ip 재사용, 외삽) 출력이 ~−95 dBFS로
수렴하는데, 허용오차 변화(1e-6↔1e-7)는 비트 동일(−311 dBFS)이라 오차가 아니라
캐소드 피드백 루프의 혼돈적 fp 증폭입니다. 두 경로 모두 같은 암묵 방정식의
1e-6 스텝 기준 유효 해로, 차이는 근사 오차가 아닌 궤적 분기입니다. 청감/스펙트럼
지표는 모두 동일하므로 수용 여부는 사용자 판단으로 남깁니다.

## 6. 남은 항목

- S2와 마찬가지로 레벨 매칭 블라인드 청취는 사용자 청취 게이트로 남습니다.
- S3(트랜스포머)는 S1 통과로 전제조건이 충족됐습니다.
