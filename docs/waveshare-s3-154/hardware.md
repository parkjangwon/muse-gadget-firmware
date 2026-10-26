# Waveshare ESP32-S3-Touch-LCD-1.54 — 하드웨어

보드 파일: `esp32/components/muse/boards/board_waveshare_s3_154.c`
(보드 이름: `Waveshare ESP32-S3-Touch-LCD-1.54`, 240×240, 네모 화면, 터치 있음).
핀 정보 출처는 waveshareteam/ESP32-S3-Touch-LCD-1.54 데모
(`09_lvgl_arduino_v9`, `01_i2s_audio`, `10_esp_sr`, `02_button_example`,
`07_sd_card_test`, `03_qmi8658_example`)와
daich/bbtalkie-waveshare-lcd154, rbouteiller/airplay-esp32 PR #90의
이 보드 실측 bring-up이다.

## SoC·메모리

- ESP32-S3R8, 플래시 16 MB, PSRAM 8 MB octal
- PSRAM 설정: octal 모드, 80 MHz, 명령·RODATA를 PSRAM에서 가져옴
  (`sdkconfig.muse-waveshare-s3-154` 참조)

## 디스플레이 — ST7789, SPI

| 신호 | GPIO |
|---|---|
| SCK | 38 |
| MOSI | 39 |
| CS | 21 |
| DC | 45 |
| RST | 40 |
| BL | 46 (LEDC 20 kHz, 10비트) |

- SPI3 호스트, 40 MHz, SPI 모드 0, BGR, 16 bpp, 240×240
- ST7789 GRAM은 240×320이지만 이 모듈의 글래스는 행 0~239다.
  Waveshare ESP-IDF 데모와 같이 gap은 0이다. Y-gap 80은 위 80줄을
  비워 두어 상단이 지지직으로 남았다.
- LVGL v9, UI 태스크 코어·우선순위는 SDK 기본(`MUSE_UI_CORE`,
  `MUSE_UI_PRIORITY`), 프레임 40 ms, 더블 버퍼 필수
- 디스플레이 슬립은 SLPIN/SLPOUT이며 GRAM은 유지된다.

## 터치 — CST816S, 공유 I2C

| 신호 | GPIO |
|---|---|
| SDA | 42 |
| SCL | 41 |
| IRQ | 48 |
| RST | 47 |

- 터치·코덱·IMU가 I2C 0번 버스를 공유한다 (내부 풀업 사용).
- 방향 플래그: `swap_xy = 1, mirror_x = 1, mirror_y = 0`
  (airplay-esp32 bring-up에서 가져옴, 실물에서 축 재확인 필요 — 아래 참조).

## 오디오 — ES8311 + ES7210, 듀플렉스 I2S

| 신호 | GPIO | 비고 |
|---|---|---|
| MCLK | 8 | 두 코덱이 핀에서 가져옴 |
| BCLK | 9 | |
| WS | 10 | |
| DOUT (스피커 출력) | 12 | |
| DIN (마이크 입력) | 11 | |
| PA_EN | 7 | 스피커 앰프 enable |

- ES8311: 스피커 DAC, I2C 주소 `0x18`, 앰프 NS4150B, 전원 5.0 V / DAC 3.3 V
- ES7210: 듀얼 마이크 ADC, I2C 주소 `0x40`, MIC1+MIC2 선택, MCLK 패드 입력,
  분주 256, 마스터 모드 아님
- I2S 0번, ESP32가 마스터, 16비트 스테레오 Philips 슬롯
- 듀얼 마이크는 믹스해서 쓴다 (`mic_slot = -1`)
- 마이크 게인은 ES7210 PGA 3 dB 단위로 스냅한다 (UI 표시와 실제 일치용)

## IMU·SD

- QMI8658 IMU: 공유 I2C 버스
- microSD: SDMMC — CLK 16, CMD 15, D0~D3 17/18/13/14

## 버튼

| 버튼 | GPIO | 역할 |
|---|---|---|
| KEY_PLUS | 4 | (보조) |
| KEY_PWR | 5 | 통화(talk) 버튼 |
| KEY_MINUS / BOOT | 0 | 보조(aux) 버튼 |

- 버튼은 왼쪽 가장자리에 있다: PWR(통화)이 위, BOOT(보조)이 아래.
- 화면 힌트 위치는 근사값이다 (`LV_ALIGN_LEFT_MID` 기준 ±30).
- PWR을 누른 채로 켜진 경우 그 눌림은 통화 입력으로 세지 않는다.

## 전원 — PMU 없음, 배터리 래치 주의

- BAT_EN (GPIO2): 배터리로 동작할 때 이 핀을 High로 유지해야 한다.
  놓으면 파워 래치가 열리고 레일이 무너진다. USB 전원에서는 무해하다.
- 배터리 전압: ADC1_CH0 (GPIO1)에 2:1 분배기로 연결, 12 dB 감쇠,
  커브 피팅 캘리브레이션 사용. 캘리브레이션이 없으면 전압 표시가 꺼진다.
- CHG_STAT (GPIO3): active low. 충전 중이면서 100% 미만일 때만
  `charging`으로 본다.
- 전압→잔량 곡선: 3.30 V 0%, 3.50 V 10%, 3.60 V 20%, 3.68 V 35%,
  3.74 V 50%, 3.82 V 65%, 3.92 V 80%, 4.02 V 90%, 4.12 V 98%, 4.20 V 100%
- 전원 끄기: 밝기 0 → 화면 끄기 → BAT_EN Low + 홀드 → AUX 버튼을 떼면
  딥슬립 진입. USB 전원에서는 레일이 유지되므로 딥슬립으로 처리한다.

## 실물에서 재확인할 것 (보드 파일 코멘트에 남은 3가지 미확정)

1. **I2S DIN/DOUT 11/12**: 벤더 예제(`10_esp_sr`)는 DIN 11이 맞고,
   bbtalkie 표는 둘이 바뀐 것으로 적혀 있다. 마이크가 이상하면 하드웨어에서
   확인할 것. 현재 코드는 DIN 11 / DOUT 12를 쓴다.
2. **터치 swap_xy / mirror_x**: airplay-esp32 bring-up 값을 그대로 썼다.
   실물에서 축이 뒤집혀 있으면 바꿀 것.
3. **ST7789 gap**: 실물에서 상단 약 80px가 노이즈라 gap 0으로 맞췄다
   (Waveshare `05_lvgl_example`과 동일). 색이 반전돼 보이면 invert를 켠다.
