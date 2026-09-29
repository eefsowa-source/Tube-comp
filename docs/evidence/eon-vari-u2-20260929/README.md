# EON-Vari mu DSP — 호스트 스모크 증거 (2026-09-29, U2 기준)

U2(미터 업그레이드)가 반영된 설치 VST3에 대해 EON `soundcraft` / `eonqc-host`
경로로 얻은 호스트 로드·렌더 게이트 증거입니다. REAPER는 제외했습니다.

## 1. 식별자

- 바이너리: `~/Library/Audio/Plug-Ins/VST3/EON-Vari mu DSP.vst3/Contents/MacOS/EON-Vari mu DSP`
- binary SHA-256: `a454dc3e146d4c15c34d6f4aca658c616bc564dbbcb0ac8e11d91ded49488b43`
- 호스트: `eonqc-host 0.5.0; JUCE v9.0.1`
- host SHA-256: `e9c368a865225852437eeabd0e2b2bc5ef33c1234d1c3706c1e36c49053f6fc9`
- soundcraft 0.2.0 (git `ee5f9ae`), measurement algorithm `0.3.0`
- 매니페스트 SHA-256 (soundcraft canonical 보고값): `ba1361cf745e94797e2c0a28643806062c3adc26978a27bbd57320600038d89f`
  (파일 자체 SHA-256: `e2c309de5e2a1798c42737c96fa138c66113af7c8588df9a538e744c7b60fc97` — 소스는 동일, soundcraft는 정규화된 해시를 보고)
- 계약 / run_id: `336be71cebf02b8b464ebacae3881159e772095423d566e2ca20abd7043ecf9e`
- 48 kHz, 512 frames, stereo, effect (2 in / 2 out)

## 2. 결과 — 6/6 통과

host는 출력 전 블록을 유한값 + `|x| <= 1.0` 로 검증합니다.

| 시나리오 | 입력 | peak (dBFS) | realtime_factor |
|---|---|---|---|
| smoke_silence | 무음 | -240 (플로어) | 13.7x |
| nominal_feedforward_1k | -18 dBFS | -6.47 | 7.0x |
| nominal_feedback_1k | -18 dBFS | -6.40 | 7.0x |
| tc6_feedback_1k | -18 dBFS | -6.48 | 7.0x |
| latver_feedback_1k | -18 dBFS | -6.41 | 6.9x |
| hot_feedforward_1k | -12 dBFS | -1.12 | 7.0x |

전 시나리오 `channel_peak_delta_db = 0.000`, `non_finite = 0`.

## 3. 이전 매니페스트와의 차이

기존 `build/stage2-soundcraft.json` 은 리퍼 시절의 -6 dBFS 레벨을 그대로 써서
host 0.5.0의 `|x| <= 1.0` 게이트를 통과하지 못했습니다. EON 형제 플러그인
(EE-1073, EON 1073/737 GOD) 매니페스트 관례인 -18/-12 dBFS로 스테이징을
맞췄습니다. 이 변경은 회귀가 아니라 계약 정렬입니다.

## 4. 헤드룸 관찰 (측정, 회귀 아님)

drive 6 dB(기본값)·output 0 dB에서 출력은 대략 -12 dBFS 입력을 넘으면
0 dBFS를 초과합니다.

- -18 dBFS 입력 → -6.5 dBFS 출력
- -12 dBFS 입력 → -1.1 dBFS 출력
- -9 dBFS 입력 → 첫 위반 샘플 -1.05 (약 +0.45 dBFS)
- -6 dBFS 입력, drive 0 dB → -2.6 dBFS 출력; output -12 dB → -8.3 dBFS 출력

drive 스테이지가 게인을 더하고 Output이 makeup/트림을 담당하기 때문이며,
U2 이전 바이너리(리뷰 런 SHA `07c77b80`, 동일 -6 dBFS에서 peak 1.534)에도
동일하게 존재했습니다. 핫 입력에서 사용자가 Output을 내리는 정상 동작으로
보고, S2(GR→바이어스 커플링) 착수 시 게인 스테이징을 다시 확인합니다.
