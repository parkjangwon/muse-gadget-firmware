# Waveshare ESP32-S3-Touch-LCD-1.54 — OTA·페어링

## OTA 릴리스

- 태그: `gadget-waveshare-s3-154-v1`
- 바이너리: `muse-gadget-waveshare-s3-154.bin`, 1.3 MB
- URL: https://github.com/parkjangwon/muse-gadget-firmware/releases/download/gadget-waveshare-s3-154-v1/muse-gadget-waveshare-s3-154.bin
- 트리거: BLE `device.ota` 명령에 위 URL을 넣는다.
- OTA는 기본적으로 꺼져 있다 (`CONFIG_HOMEHUB_OTA_ENABLED=n`,
  버전 `999.0.0`). 풀 UI 보드는 예외다. 버전은 `-DPROJECT_VER=1.0.0`
  또는 `version.txt`로 지정한다.

## 첫 부팅·페어링

상태는 화면과 상태 표시로 확인한다.

| 표시 | 의미 |
|---|---|
| 주황, 숨쉬듯 | BLE advertising, 설정 대기 |
| 파랑, 숨쉬듯 | 페어링에 물리 버튼 누름 필요 |
| 파랑 | Wi-Fi 또는 VM 세션 연결 중 |
| 초록 | 연결됨 (터널 또는 제어 세션 연결) |
| 노랑, 깜빡임 | VM 전환 또는 연결 끊김 |
| 보라 | 페어링 해제됨 |
| 빨강, 깜빡임 | 에러 |

- 페어링 전에는 화면에 가젯 이름이 뜬다 (어느 가젯을 고를지 구분용).
  페어링이 되거나 토큰이 수동 설정되면 이름이 사라지고 마이크 아이콘과
  스피커 버튼이 나타난다. 그전까지는 눌러도 Muse에 닿지 않으므로 숨긴다.
- 커뮤니티 페어링 v5를 쓴다. 폰 앱이 v5를 지원하고 커뮤니티 기기를
  목록에 보여줘야 한다. 버튼 누름은 필요하지만 제조사 증명이 없어서
  진행 중인 중간자 공격을 막지 못한다.
- Wi-Fi 프로비저닝을 BLE로 하지 않고 반복 테스트하려면
  `CONFIG_HOMEHUB_WIFI_SSID`와 `CONFIG_HOMEHUB_WIFI_PASSWORD`를
  menuconfig에서 설정한다. 그래도 토큰용 페어링은 한 번 필요하다.

## 버튼 동작

- **짧게 누르기**: 대기 중인 페어링을 확인하거나, 설정이 끝나지
  않았다면 설정 창을 다시 연다.
- **5초 길게 누르기**: 설정 초기화 (페어링 해제 + Wi-Fi 삭제).
- 이 보드에서는 PWR이 통화(talk), BOOT이 보조(aux)다.

## 남은 설정 (2026-10-08 기준)

1. OpenRouter 키 설정 — 펌웨어에 키가 없어서 TTS 음성이 안 나오고
   자막만 나온다.
2. 폰으로 BLE 페어링 + Wi-Fi 설정 — 2026-10-08 erase-flash 이후
   페어링이 지워진 상태가 마지막 확인 상태다.
3. TTS 보이스 `af_heart` 적용은 키 설정 후 OTA로 업데이트한다.
