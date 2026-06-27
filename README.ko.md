# DualSense on Switch

Raspberry Pi Pico 2 W를 이용해 DualSense를 Nintendo Switch Pro Controller처럼 사용할 수 있게 해주는 펌웨어입니다.

Credit: 이 프로젝트는 [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)의 포크는 아니지만, 해당 프로젝트에서 큰 영감을 받았고 구현에도 많은 참고를 하였습니다.

## 할 수 있는 것

- DualSense를 Bluetooth로 연결하고, Switch/PC에는 `Pro Controller`로 인식시킵니다.
- DualSense 터치패드 클릭은 Switch 캡처 버튼으로 동작합니다.
- 모션 컨트롤과 진동을 지원합니다.
- DualSense 3.5 mm 단자에 이어폰/헤드셋을 꽂으면 USB 오디오 출력으로 사용할 수 있습니다.
- DualSense 마이크를 USB 마이크로 사용할 수 있습니다.

## 확인된 동작

- PC: 3.5 mm 잭 오디오 출력과 마이크가 모두 작동합니다.
- Nintendo Switch 1: 3.5 mm 잭 오디오 출력이 작동합니다.
- Nintendo Switch 2: 3.5 mm 잭 오디오 출력과 마이크가 모두 작동합니다.

## 준비물

- Raspberry Pi Pico 2 W
- Sony DualSense 컨트롤러
- Pico 2 W를 Switch/PC에 연결할 USB 케이블
- 빌드된 펌웨어 파일: `build\pico2_w\ds5onswitch.uf2`

## 펌웨어 설치

1. Pico 2 W의 BOOTSEL 버튼을 누른 채 PC에 USB로 연결합니다.
2. PC에 나타난 USB 드라이브에 `ds5onswitch.uf2`를 복사합니다.
3. 복사가 끝나면 Pico 2 W가 자동으로 재부팅됩니다.

이미 이 펌웨어가 올라가 있다면 BOOTSEL 버튼을 4번 클릭해서 USB 드라이브 모드로 들어갈 수 있습니다.

## 페어링

1. Pico 2 W를 Switch나 PC에 연결합니다.
2. Pico 2 W의 LED가 깜빡이면 컨트롤러 연결 대기 상태입니다.
3. DualSense에서 PS + Create 버튼을 길게 눌러 페어링 모드로 진입합니다.
4. 연결되면 Pico 2 W의 LED가 켜진 상태로 유지되고, Switch/PC에는 `Pro Controller`가 나타납니다.

한 번 페어링한 DualSense는 다음부터 전원을 켜면 다시 연결됩니다.

## 오디오

- DualSense에 이어폰이나 헤드셋을 꽂으면 Switch/PC 오디오 출력 장치로 사용할 수 있습니다.
- 이어폰/헤드셋을 빼면 오디오 출력 장치는 사라지고, 컨트롤러와 마이크 기능은 계속 사용할 수 있습니다.
- DualSense 마이크는 컨트롤러가 연결된 동안 USB 마이크로 노출됩니다.
- DualSense의 마이크 음소거 버튼을 누르면 마이크가 음소거되고 DualSense의 음소거 LED가 켜집니다.

오디오 장치가 바뀔 때 USB가 잠깐 다시 연결되는 것처럼 보일 수 있습니다. 정상 동작입니다.

## BOOTSEL 버튼

Pico 2 W의 BOOTSEL 버튼은 아래 기능으로 사용할 수 있습니다.

| 조작 | 동작 |
| --- | --- |
| 1번 클릭 | 일반 LED 표시 on/off 토글 |
| 2번 클릭 | 현재 컨트롤러 연결 해제, 또는 연결된 컨트롤러가 없으면 30초 페어링 스캔 시작 |
| 3번 클릭 | 펌웨어 재부팅 |
| 4번 클릭 | USB 드라이브 모드로 재부팅 |
| 길게 누르기 | 저장된 컨트롤러 페어링 삭제 |

LED off 상태에서도, 길게 누르기 완료를 알리는 6회 점멸 같은 특수 LED 표시는 계속 동작합니다.

## LED 상태

- 깜빡임: 컨트롤러 연결 대기 중
- 켜짐: DualSense 연결됨
- 6회 점멸: 저장된 페어링 삭제 완료

## 직접 빌드

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-pico-windows.ps1
powershell -ExecutionPolicy Bypass -File tools\test-host.ps1
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

빌드가 끝나면 펌웨어 파일은 아래 경로에 생성됩니다.

```text
build\pico2_w\ds5onswitch.uf2
```

## 문제 해결

- Switch/PC에 컨트롤러가 보이지 않으면 DualSense가 Pico 2 W에 연결되어 있는지 확인하세요. DualSense가 연결되지 않은 상태에서는 컨트롤러 장치를 노출하지 않습니다.
- 다른 DualSense를 연결하고 싶으면 BOOTSEL을 2번 클릭해서 현재 연결을 끊은 뒤 새 컨트롤러를 페어링하세요.
- 자동 재연결이 꼬이면 BOOTSEL을 길게 눌러 저장된 페어링을 삭제한 뒤 다시 페어링하세요.
- 오디오 출력이 보이지 않으면 DualSense의 3.5 mm 단자에 이어폰/헤드셋이 꽂혀 있는지 확인하세요.
