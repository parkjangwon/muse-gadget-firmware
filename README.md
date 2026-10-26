# Muse Gadget Firmware — Waveshare ESP32-S3-Touch-LCD-1.54

Waveshare ESP32-S3-Touch-LCD-1.54 보드를 Muse 전용 가젯으로 만드는 펌웨어다.
Meta의 오픈소스 muse-gadget-sdk(기준 커밋 `74a5e2d`)에 이 보드용 포트와
OpenRouter TTS를 얹은 것이다. 이 보드는 SDK 지원 목록에 없어서 디스플레이
드라이버부터 직접 포팅했다. 벤더 BSP는 ESP Component Registry에 없으므로
패널·터치·코덱을 직접 구동한다(AIPI Lite 패턴).

## 디바이스 한눈에 보기

| 항목 | 스펙 |
|---|---|
| SoC | ESP32-S3R8 |
| 플래시 / PSRAM | 16 MB / 8 MB octal PSRAM |
| 디스플레이 | 1.54" 240×240 ST7789 TFT, SPI |
| 터치 | CST816S, I2C |
| 오디오 | ES8311 스피커(앰프 NS4150B) + ES7210 듀얼 마이크 ADC, 듀플렉스 I2S |
| 기타 | QMI8658 IMU, microSD, 버튼 3개 |
| 전원 | PMU 없음, GPIO2 배터리 래치 |
| 보드 파일 | `esp32/components/muse/boards/board_waveshare_s3_154.c` |
| 설정 오버레이 | `esp32/devices/sdkconfig.muse-waveshare-s3-154` |

자세한 핀맵과 주변장치는 [docs/waveshare-s3-154/hardware.md](docs/waveshare-s3-154/hardware.md)를 본다.

## 빠른 시작

전제 조건: ESP-IDF v6.0.1, SDK 토큰(`mgst_…`, gadgets.muse.ai > Account >
SDK tokens). 토큰은 절대 커밋하거나 전문을 노출하지 않는다.

```sh
# ESP-IDF 활성화 후, esp32/ 디렉토리에서 수동 빌드 (이 보드는 board.sh 프로파일이 없다)
idf.py -B build-muse-waveshare-s3-154 -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-muse-waveshare-s3-154/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-waveshare-s3-154" build
idf.py -B build-muse-waveshare-s3-154 -p PORT flash
idf.py -B build-muse-waveshare-s3-154 -p PORT monitor
```

빌드·플래시·모니터·파티션·함정은
[docs/waveshare-s3-154/build-flash.md](docs/waveshare-s3-154/build-flash.md),
음성 TTS는
[docs/waveshare-s3-154/voice-tts.md](docs/waveshare-s3-154/voice-tts.md),
OTA·페어링은
[docs/waveshare-s3-154/ota-pairing.md](docs/waveshare-s3-154/ota-pairing.md),
그동안 겪은 문제와 해결은
[docs/waveshare-s3-154/troubleshooting.md](docs/waveshare-s3-154/troubleshooting.md)를 본다.

## OTA 릴리스

- 태그: `gadget-waveshare-s3-154-v1`, 바이너리 1.3 MB
- URL: https://github.com/parkjangwon/muse-gadget-firmware/releases/download/gadget-waveshare-s3-154-v1/muse-gadget-waveshare-s3-154.bin
- BLE `device.ota` 명령에 이 URL을 넣어 트리거한다.

## 동작 확인 이력 (날짜 기준, 현재 상태 단정 아님)

- 2026-10-06: 빌드·플래시 성공, 첫 부팅 AUDIO INIT FAILED 2건 수정 후
  Wi-Fi/BLE 페어링 완료, 화면 `ready` 확인.
- 2026-10-06 01:46: OTA용 빌드 성공, 위 릴리스 발행.
- 2026-10-08: erase-flash 후 복구 플래시 완료, BLE advertising 중·Wi-Fi
  미설정(페어링 대기) 상태가 마지막 확인 상태다. 이후 상태는 이 문서가
  보장하지 않는다.
- 남은 설정(2026-10-08 기준): OpenRouter 키 미설정(자막만 나옴),
  폰으로 BLE 페어링 + Wi-Fi 설정, 키 설정 후 OTA로 `af_heart` 보이스 적용.

## 라이선스·주의

- 업스트림 muse-gadget-sdk는 Apache 2.0이다. 서드파티 코드(`minimp3.h` CC0,
  `main/pixel_font.c` BSD-2-Clause)는 기존 라이선스 헤더를 유지한다.
- Jollybot 아바타(`avatar/`)는 Apache 대상이 아니다. 헤더를 바꾸지 않는다.
- 문서·화면·로그·설정 라벨에는 Muse라는 이름만 쓴다.
- Secure Boot는 켜지 않는다. 앱은 커밋된 `dev_signing_key.pem`으로 서명되며
  eFuse를 굽지 않으므로 계속 다시 플래시할 수 있다.
