# Waveshare ESP32-S3-Touch-LCD-1.54 — 음성 TTS

## SDK 기본 동작과의 차이

SDK 기본값은 텍스트 자막만 보여주고 소리가 나지 않는다 (무음 재생).
이 펌웨어는 Muse의 답변 텍스트를 OpenRouter TTS로 음성화해서 스피커로
재생한다. TTS가 실패하면 무음 자막으로 폴백한다 (기존 동작 유지).

## 구현

- 위치: `esp32/components/muse/muse_chat_session.cpp`
  (`fetch_tts_openrouter`, `tts_data`)
- 흐름: 답변 텍스트를 `POST https://openrouter.ai/api/v1/audio/speech`로
  보내고, 돌아온 MP3를 `tts_data()`로 스트리밍해서 디코더에 넘긴 뒤
  `mp3_ended`로 마무리한다.
- 키가 없으면 `TTS: no OpenRouter key (CONFIG_MUSE_OPENROUTER_KEY)` 경고를
  남기고 자막만 보여준다.

## 설정 (Kconfig)

| 옵션 | 기본값 | 비고 |
|---|---|---|
| `MUSE_OPENROUTER_KEY` | `""` (빈 값) | OpenRouter API 키. 빈 값이면 자막만 |
| `MUSE_TTS_MODEL` | `hexgrad/kokoro-82m` | |
| `MUSE_TTS_VOICE` | `af_heart` | |

`idf.py menuconfig`에서 바꾸거나 빌드 디렉토리 `sdkconfig`에 넣는다.
키는 절대 커밋하지 않는다.

## 왜 Kokoro인가

처음 기본값은 `google/gemini-3.8-flash-lite-tts` + 보이스 `Kore` +
MP3이었으나, VM에서 OpenRouter 엔드포인트를 직접 테스트한 결과
Gemini TTS는 MP3를 거부했다
(`400 "Gemini TTS only supports response_format=pcm"`).
펌웨어 MP3 디코더와 맞지 않으므로 기본값을 바꿨다.

- `hexgrad/kokoro-82m` + `af_heart` + mp3 → 200, 유효한 MP3 13,581바이트 확인
- `openai/gpt-4o-mini-tts` → 400 `Model does not exist`
- 결론: 모델 기본값 Kokoro, 보이스 기본값 `af_heart`

## 알려진 문제

- **기기에서 OpenRouter HTTPS 실패 (2026-10-06)**: 기기는
  `TTS: OpenRouter HTTP 0 err=28674 (0x7002=ESP_ERR_HTTP_CONNECT)
  sock_errno=0 in ~980ms`로 실패했지만, 같은 요청이 VM에서는 정상
  동작했다. 기기 특유 문제로 보고, 화면 시계가 00:29에 멈춰 있던
  점에서 시간 미동기화(TLS 검증 실패)를 의심했다. 다음 단계는
  인증서 검증 우회 테스트 → 되면 SNTP 시간 동기화 추가, 안 되면
  DNS/TCP 조사다. 아직 해결되지 않았다.
- **mbedtls PSA 충돌 (2026-10-08 재빌드)**: `esp_http_client`를 포함한
  재빌드가 PSA crypto init에서 크래시하며 부팅 루프에 빠졌다
  (`0x42068d78`). 자세한 이력은 troubleshooting 문서를 본다.
  현재 트리의 TTS 코드와 2026-10-08 재빌드는 같은 코드가 아니므로,
  이 문제를 현재 트리에 단정하지 않는다.
