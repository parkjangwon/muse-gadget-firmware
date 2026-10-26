# Waveshare ESP32-S3-Touch-LCD-1.54 — 문제 해결 기록

날짜 기준 기록이다. "현재 트리"는 이 레포에 커밋된
포트 + TTS 상태를 말하고, 2026-10-08 재빌드는 맥미니의 별도 빌드라
같은 코드가 아닐 수 있다. 날짜를 보고 구분한다.

## 2026-10-06: 첫 부팅 `ERROR` (AUDIO INIT FAILED)

빌드·플래시 성공 후 첫 부팅에서 오디오 초기화 실패 화면이 떴다.
원인 2가지, 둘 다 고치고 재빌드·재플래시 후 해소됐다.

1. **LVGL v9에서 `LV_ALIGN_LEFT` 제거됨** → `LV_ALIGN_LEFT_MID`로 교체.
2. **ES7210 I2C 주소에 `>> 1` 오기입** (`0x28`로 접근) → 시프트 제거,
   `0x40`으로 접근.

이후 Wi-Fi/BLE 페어링 완료, 화면 `ready` 확인.

## 2026-10-06 새벽: 기기에서 OpenRouter HTTPS 실패

- 증상: `TTS: OpenRouter HTTP 0 err=28674 (0x7002=ESP_ERR_HTTP_CONNECT)
  sock_errno=0 in ~980ms`
- VM에서는 같은 요청이 정상 동작 → 기기 특유 문제.
- 의심: 화면 시계 00:29 표시 = 시간 미동기화에 따른 TLS 검증 실패.
  인증서 검증 우회 테스트를 하고, 되면 SNTP 시간 동기화를 추가할
  계획이었다. 안 되면 DNS/TCP 조사. 미해결.
- 같은 새벽 장원 사진 제보(01:34): 기기 ERROR 화면 + 프레임버퍼 깨짐
  → OTA 패치 경로로 대응했다.

## 2026-10-06 01:46: OTA 빌드 성공

맥미니 빌드 환경(Python 3.12.8, ESP-IDF v6.0.1)에서
port-v2 + TTS 패치를 적용하고 장원 sdkconfig을 복사해 빌드했다.
도중 LED 백엔드 충돌(`CONFIG_HOMEHUB_LED_BACKEND_DEVKIT_GPIO27`이
S3 SPI 플래시 핀과 충돌) → `CONFIG_HOMEHUB_LED_BACKEND_MUSE=y`로
해결, LED 백엔드 중복 설정 줄을 지우고 클린 빌드했다.
결과 `muse-gadget.bin` 1.3 MB, 릴리스 `gadget-waveshare-s3-154-v1` 발행.

## 2026-10-08 새벽: USB 플래시 시도 실패 2건

1. **TTS 포함 재빌드가 PSA crypto init에서 크래시** (부팅 루프,
   `0x42068d78`). 추정 원인은 `esp_http_client` 포함이 mbedtls 설정을
   깨뜨린 것. 2026-10-06 01:46 릴리스도 테스트 안 된 빌드라 같은
   문제를 안고 있었을 수 있다.
2. **TTS 제외 재빌드도 sdkconfig 재구성 실패**: 장원 원본 sdkconfig이
   없어서 LVGL 폰트, 파티션 크기 등 새 에러가 계속 나왔다 (8회 시도).

당시 상태: 기기는 부팅 루프 (깨진 펌웨어가 플래시된 상태).
다음 할 일로 남은 것: 장원의 2026-10-06 sdkconfig 확보, TTS 패치의
mbedtls 문제 해결, 함께 빌드·플래시.

## 2026-10-08 아침: 복구 성공

3.5시간 작업 끝에 성공했다. 고친 것:

1. TTS 제외 (mbedtls PSA 문제 회피).
2. `sdkconfig.defaults.esp32s3`로 sdkconfig 재구성 (원본 없음).
3. LVGL 폰트 5개 활성화.
4. 파티션 오버플로우 4 KB → 그 빌드에 한해 `ota_0`을 `0x210000`으로
   확장 (현재 트리 `partitions_muse.csv`에는 이 변경이 없다).
5. `-Werror=format-truncation` → pragma로 무시 (임시 조치).

결과: 빌드 성공, 플래시 완료, 부팅 확인. BLE advertising 중,
Wi-Fi 미설정 (페어링 대기) 상태가 마지막 확인 상태다.

## 환경 함정

- `IDF_PYTHON_ENV_PATH`가 예전 v5.5를 가리키면 cmake가 실패한다.
  `unset IDF_PYTHON_ENV_PATH` 후 `export.sh`를 실행한다.
- 한 번 생성된 빌드 디렉토리 `sdkconfig`가 오버레이 수정을 덮어쓴다.
  오버레이를 바꾼 뒤에는 빌드 디렉토리를 지우고 다시 빌드한다.
- 보드 식별: 플래시 전에 어떤 보드가 연결됐는지 확인한다.
  실행 중인 펌웨어에 `tools/muse/chat.py --status`로 물어보거나,
  USB 디스크립터(`ioreg -p IOUSB` / `lsusb -v`)나
  `python -m esptool -p PORT chip-id`로 칩을 확인한다.
  잘못된 보드 프로파일을 쓰면 핀맵·플래시 크기·상태 백엔드가
  통째로 틀어진다.
