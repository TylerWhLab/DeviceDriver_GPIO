# Device Driver GPIO control

ledkey_dev.c : device driver
ledkey_app.c : user space app

Device Driver에서 ioctl, interrupt, kernel timer 등을 활용하여 GPIO를 제어하였습니다.
ubuntu 20, 22 에서 동작하고 크로스 컴파일 환경 구성이 필요합니다.

interrupt로 입력 한 스위치 값을 읽고, 읽은 값에 따라 timer 시작/종료, timer 주기 변경, 점멸 LED 변경 할 수 있도록 하였습니다. 
![image](https://github.com/TylerWhLab/DeviceDriver_GPIO/assets/75075900/e230870c-ba37-43a9-bf58-070af614bd20)
