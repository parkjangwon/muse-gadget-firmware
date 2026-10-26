# Waveshare ESP32-S3-Touch-LCD-1.54 — 빌드·플래시

## 전제 조건

- ESP-IDF v6.0.1 (다른 버전은 미지원. `gcm.h`, `gpio_ll.h` 같은 헤더
  없음 에러는 대개 잘못된 IDF 태그다). 활성화:
  `. $IDF_PATH/export.sh` (없으면 `~/esp/esp-idf-v6.0.1`,
  `~/esp/esp-idf-v6`, `~/esp/esp-idf`를 찾는다)
- Python 3, C/C++ 컴파일러 (`cc`, `c++`)
- USB 데이터 케이블. 샌드박스 에이전트는 보통 샌드박스 밖에서
  플래시·모니터를 실행해야 한다.
- SDK 토큰(`mgst_…`, gadgets.muse.ai > Account > SDK tokens).
  빌드 디렉토리 `sdkconfig`에 `CONFIG_GADGET_SDK_TOKEN="mgst_…"`을
  넣거나 `idf.py menuconfig`로 설정한다. 없으면 빌드 경고가 나고,
  Muse가 토큰을 요구하기 시작하면 페어링이 멈춘다.
  **토큰은 절대 커밋하거나 전문을 출력하지 않는다.**

## 빌드

`esp32/`에서 실행한다. 헬퍼가 ESP-IDF v6.0.1을 찾고
`devices/sdkconfig.muse` 다음 `devices/sdkconfig.muse-waveshare-s3-154`를 얹는다.
로그는 `/tmp/muse_build_s3154.log`다.

```sh
export IDF_EXPORT="$HOME/esp/esp-idf-v6/export.sh"   # 이미 v6.0.1이면 생략
unset IDF_PYTHON_ENV_PATH
tools/muse/board.sh build s3154
```

참고로 맥미니 빌드 환경에서는 `build-waveshare` 디렉토리를 썼다.
디렉토리 이름은 달라도 되지만, 한 번 생성된 `sdkconfig`가 오버레이
수정을 덮어쓰므로 오버레이를 바꾼 뒤에는 빌드 디렉토리(또는 그 안의
`sdkconfig`)를 지우고 다시 빌드한다. `cmake/validate_config.cmake`이
오래된 TCP 버퍼 크기나 cJSON 중첩 제한을 가진 설정이면 빌드를 멈춘다.

- `managed_components/`와 `dependencies.lock`은 보드 공용이다.
  풀 UI 보드와 그 외 보드를 오가다 컴포넌트 매니저가 실패하면
  (대개 lvgl) 둘 다 지우고 다시 빌드한다.
- 이 빌드는 `partitions_muse.csv`를 쓰고 플래시 16 MB가 필요하다.

## 플래시

```sh
tools/muse/board.sh flash s3154          # 포트는 USB Serial/JTAG로 찾는다
idf.py -B build-muse-waveshare-s3-154 -p PORT monitor   # Ctrl-] 로 종료
```

- 네이티브 USB라 포트는 대개 `/dev/cu.usbmodem*` (macOS) 또는
  `/dev/ttyACM*` (Linux)이다. Linux는 `dialout` (또는 `uucp`) 그룹에
  들어가야 한다. 부트로더 진입이 안 되면 BOOT을 누른 채 RESET을
  눌렀다 BOOT을 뗀다.
- `flash`는 부트로더, 파티션 테이블(오프셋 `0x10000`, IDF 기본값
  `0x8000`이 아니다), otadata, phy_init, 앱을 `ota_0`에 쓴다.
  NVS는 건드리지 않으므로 페어링·Wi-Fi 자격 증명은 리플래시에도 남는다.
- `idf.py -p PORT erase-flash`는 페어링까지 전부 지운다.
  처음부터 시작할 때만 쓴다.
- 앱은 커밋된 `dev_signing_key.pem`으로 서명한다. Secure Boot은
  켜지 않는다. eFuse를 굽지 않으므로 계속 다시 플래시할 수 있다.
  `CONFIG_SECURE_BOOT`, 플래시 암호화,
  `CONFIG_HOMEHUB_PAIRING_EFUSE_AUTH`를 켜지 않는다.

## 모니터·부팅 확인

- 정상 부팅 로그: `link.main: Muse Gadget starting`
- 보드 이름 로그: `muse: board: Waveshare ESP32-S3-Touch-LCD-1.54`
- 에이전트가 TTY 없이 읽을 때는 DTR/RTS를 건드리지 않는 읽기 전용
  캡처를 쓴다 (리셋 방지). 부팅부터 보려면 `tools/muse/monitor.py PORT`
  (리셋 후 캡처, pyserial 필요).

## 이 보드에서 겪은 빌드 함정

- **LED 백엔드 충돌**: LED 스트립 GPIO가 S3 SPI 플래시 핀과 충돌한다.
  `CONFIG_HOMEHUB_LED_BACKEND_DEVKIT_GPIO27`을 쓰지 말고
  `CONFIG_HOMEHUB_LED_BACKEND_MUSE=y`를 쓴다 (디스플레이에 상태 표시).
  `sdkconfig.muse`에 이미 그렇게 되어 있다.
- **IDF 환경 꼬임**: `IDF_PYTHON_ENV_PATH`가 예전 v5.5를 가리키면
  cmake가 실패한다. `unset IDF_PYTHON_ENV_PATH` 후 `export.sh`를
  실행한다.
- **LVGL 폰트 누락**: 2026-10-08 복구 빌드에서 폰트 5개를 활성화해야
  했다. 폰트 에러가 나면 menuconfig에서 LVGL 폰트를 확인한다.
- **파티션 오버플로우**: 2026-10-08 복구 빌드에서 4 KB가 넘쳐
  그 빌드에 한해 `ota_0`을 `0x210000`으로 늘렸다. 현재 트리의
  `partitions_muse.csv`에는 이 변경이 없으므로, 같은 에러가 나면
  파티션 크기를 다시 잰다.
- **포맷 경고**: `-Werror=format-truncation`은 2026-10-08 복구 빌드에서
  pragma로 무시했다. 근본 수정이 아니므로 다시 나면 코드를 고친다.
