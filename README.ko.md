# Pico Nintendo Switch 2 깨우기 동글

Raspberry Pi Pico 2 W를 이용해 다양한 컨트롤러를 Nintendo Switch 1/2에서 사용 가능하게 하는 펌웨어입니다.  
지원되는 어떤 컨트롤러를 사용해도 Switch 2 슬립모드를 깨울 수 있습니다. (※Switch 1 깨우기는 지원하지 않습니다.)

지원 컨트롤러 목록:

| 컨트롤러 | 비고 |
| --- | --- |
| DualSense | DualSense 마이크 및 3.5mm 오디오 지원 |
| DualShock 4 | - |
| Nintendo Switch Pro Controller 1/2 | - |
| Steam Controller | 블루투스 모드 (Steam + L1 + B) |
| Xbox Bluetooth | Xbox One S 컨트롤러와 Xbox Elite 2 테스트됨. 그 외에는 동작 확인되지 않음. |

Credits:  
컨트롤러 동작을 분석하는 데에 [ataradov/usb-sniffer](https://github.com/ataradov/usb-sniffer)와 [homewsn/blesniff](https://github.com/homewsn/blesniff)가 큰 도움이 되었습니다.  
[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)에서 큰 영감을 받았고 듀얼센스 파트 구현에도 많은 참고를 하였습니다.

## 할 수 있는 것

- 각 컨트롤러를 `Nintendo Switch Pro Controller`로 인식시킵니다.
- Nintendo Switch 2를 슬립 상태에서 깨울 수 있습니다.
- 모션 컨트롤과 진동을 지원합니다. 단, Xbox 컨트롤러는 모션 컨트롤을 지원하지 않습니다.
- 스위치 1/2에서 DualSense의 3.5 mm 단자를 이용 가능합니다. 스위치 2에서는 DualSense의 마이크도 이용 가능합니다.
- 부수적인 유용한 기능으로, 스위치 1 혹은 PC에서 무선으로 프로콘 2를 사용할 수 있습니다.(프로콘 1로 인식됩니다.)

## 준비물

- Raspberry Pi Pico 2 W
- Sony DualSense 컨트롤러, DualShock 4, Nintendo Switch Pro Controller 1, Steam Controller, 또는 Xbox Bluetooth 컨트롤러
- Pico 2 W를 Switch/PC에 연결할 micro USB 케이블
- [최신 GitHub Release](https://github.com/david419kr/wake-NS2-dongle/releases/latest)의 펌웨어 파일

## 펌웨어 설치

1. Pico 2 W의 BOOTSEL 버튼을 누른 채 PC에 USB로 연결합니다.
2. [최신 GitHub Release](https://github.com/david419kr/wake-NS2-dongle/releases/latest)에서 `switchbridge.uf2`를 다운로드합니다.
3. PC에 나타난 USB 드라이브에 `switchbridge.uf2`를 복사합니다.
4. 복사가 끝나면 Pico 2 W가 자동으로 재부팅됩니다.

이미 이 펌웨어가 올라가 있다면 BOOTSEL 버튼을 4번 클릭해서 USB 드라이브 모드로 들어갈 수 있습니다.

## 페어링

1. Pico 2 W를 Switch에 연결합니다.
2. Pico 2 W의 LED가 깜빡이면 컨트롤러 연결 대기 상태입니다.
3. 각 컨트롤러를 블루투스 페어링 모드로 진입시킵니다.
4. 연결되면 Pico 2 W의 LED가 켜진 상태로 유지되고, Switch에는 `Pro Controller`가 나타납니다.

한 번 페어링한 컨트롤러는 다음부터 전원을 켜면 다시 연결됩니다.

## Nintendo Switch 2 깨우기

최초 1회, Switch 2를 켠 상태에서 Pico 2 W를 Switch 2에 USB로 연결하면 Switch 2 깨우기에 필요한 정보가 저장됩니다.

1. Switch 2를 켭니다. 켠 상태에서 Pico 2 W를 Switch 2에 연결합니다.
2. 이후, 컨트롤러의 PS/Home/Steam/Xbox 버튼을 눌러 Switch 2를 깨울 수 있습니다.

Switch 2가 깨어난 타이밍에 Bluetooth 연결이 한 번 끊기므로, PS/Home/Steam/Xbox 버튼을 한 번 다시 눌러줘야 할 수 있습니다. 컨트롤러 기종에 따라 버튼 조작 필요 없이 자동으로 재연결 되는 경우도 있습니다.

Switch 1 깨우기는 지원하지 않습니다.

## 각 컨트롤러 별 특수 키매핑

| 컨트롤러 | 조작 |
| --- | --- |
| DualSense / DS4 | 터치패드 클릭은 "스크린샷 캡쳐" 버튼으로 인식됩니다. |
| Xbox Controller | Back/View + Menu를 동시에 누르면 "스크린샷 캡쳐" 버튼으로 인식됩니다. |
| Steam Controller | 좌 터치패드는 8방향 D-pad, 우 터치패드는 우 스틱, QAM(・・・) 버튼은 스크린샷 캡쳐로 인식됩니다. |
| Steam Controller | 좌 터치패드의 정중앙을 클릭 한 상태에서 백버튼(L4/R4/L5/R5)을 누르고, 이어서 할당할 버튼을 누르면 리매핑 됩니다. |
| Switch 2 Pro Controller | C 버튼을 누른 상태에서 백버튼(GL/GR)을 누르고, 이어서 할당할 버튼을 누르면 리매핑 됩니다. |

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
| 1번 클릭 | LED 표시 on/off 토글. 전원을 껐다 켜도 설정이 유지됩니다. |
| 2번 클릭 | 현재 컨트롤러 연결 해제, 또는 연결된 컨트롤러가 없으면 30초 페어링 스캔 시작 |
| 3번 클릭 | 펌웨어 재부팅 |
| 4번 클릭 | USB 드라이브 모드로 재부팅 |
| 길게 누르기 | 저장된 컨트롤러 페어링 삭제 |

## LED 상태

- 깜빡임: 컨트롤러 연결 대기 중
- 켜짐: 컨트롤러 연결됨
- 6회 점멸: 저장된 페어링 삭제 완료

## 직접 빌드

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-pico-windows.ps1
powershell -ExecutionPolicy Bypass -File tools\test-host.ps1
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

빌드가 끝나면 펌웨어 파일은 아래 경로에 생성됩니다.

```text
build\pico2_w\switchbridge.uf2
```

## 문제 해결

- Switch에 컨트롤러가 보이지 않으면 컨트롤러가 Pico 2 W에 연결되어 있는지 확인하세요. Bluetooth 컨트롤러가 연결되지 않은 상태에서는 짧은 Switch 2 Pro 등록용 handshake 외에는 컨트롤러 장치를 노출하지 않습니다.
- 다른 컨트롤러를 연결하고 싶으면 BOOTSEL을 2번 클릭해서 현재 연결을 끊은 뒤 새 컨트롤러를 페어링하세요.
- 자동 재연결이 꼬이면 BOOTSEL을 길게 눌러 저장된 페어링을 삭제한 뒤 다시 페어링하세요.
- DualSense 사용시 오디오 출력이 보이지 않으면 DualSense의 3.5 mm 단자에 이어폰/헤드셋이 꽂혀 있는지 확인하세요.

### Switch 2 깨우기가 동작하지 않을 때

일반적인 사용에서는 수동 설정이 필요 없습니다. 그래도 만약 컨트롤러를 페어링하고 Switch 2에 연결한 뒤 Switch 2 깨우기가 동작하지 않는 경우, 문제 해결용으로 아래 수동 등록을 시도해 보세요.

1. 컨트롤러를 Pico 2 W와 페어링하고, Pico 2 W를 Switch 2에 연결합니다.
2. 수동 등록 모드로 전환합니다.

| 컨트롤러 | 조작 |
| --- | --- |
| DualSense / DS4 | PS 버튼을 누른 상태에서 터치패드를 2번 클릭합니다. |
| Switch Pro Controller 1/2 | Home 버튼을 누른 상태에서 Capture 버튼을 2번 누릅니다. |
| Steam Controller | Steam 버튼을 누른 상태에서 QAM(・・・) 버튼을 2번 누릅니다. |
| Xbox Controller | Xbox 버튼을 누른 상태에서 Back/View 버튼을 2번 누릅니다. |

3. `Switch 2 Pro Controller`로 잠시 연결된 후, Switch 2가 이 컨트롤러를 인식한 뒤 자동으로 일반 모드로 복귀합니다.

4. Switch 2를 슬립 모드로 둡니다.
5. 컨트롤러의 PS/Home/Steam/Xbox 버튼을 눌러 Switch 2를 깨웁니다.
