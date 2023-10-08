#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include "ioctl_test.h" // ioctl을 사용하기 위해 추가한 header 

#define DEVICE_FILENAME "/dev/ledkey"

int main(int argc, char *argv[]) // led   timer
{
	int dev;
	char key_no;
	char led_no;
	char timer_val;
	int ret;
	int cnt = 0;
	int loopFlag = 1;
	struct pollfd Events[2];
	char inputString[80];
	keyled_data info; // ioctl_test.h 참조

	if(argc != 3)
	{ // led 초기값, tim
        printf("Usage : %s [led_val(0x00~0xff)] [timer_val(1/100)]\n",argv[0]);
		return 1;
	}
	led_no = (char)strtoul(argv[1],NULL,16);
	if(!((0 <= led_no) && (led_no <= 255)))
	{
		printf("Usage : %s [led_data(0x00~0xff)]\n",argv[0]);
		return 2;
	}
	printf("Author:KJK\n");
    //printf("1: %s\n2: %s\n", argv[1], argv[2]);
    timer_val = atoi(argv[2]);
	info.timer_val = timer_val;

//	dev = open(DEVICE_FILENAME, O_RDWR | O_NONBLOCK);
	dev = open(DEVICE_FILENAME, O_RDWR ); // blocking
	if(dev < 0)
	{
		perror("open");
		return 2;
	}

	ioctl(dev,TIMER_VALUE,&info); // 커널 타이머의 타이머 밸류 변경
    write(dev, &led_no, sizeof(led_no));
    ioctl(dev,TIMER_START); //TIMER_START 명령이 오면 타이머 돌려라 

	memset( Events, 0, sizeof(Events));

	Events[0].fd = dev; // 디바이스 드라이버
	Events[0].events = POLLIN;
	Events[1].fd = fileno(stdin); // 키보드
	Events[1].events = POLLIN;

	while(loopFlag)
	{

		ret = poll(Events, 2, 1000); // 2 : 2개의 장치 // 1000 : 1초마다 
		if(ret==0)
		{
  		//printf("poll time out : %d\n",cnt++);
			continue;
		}
		if(Events[0].revents & POLLIN)  // 스위치 입력 처리
		{
    		read(dev, &key_no, sizeof(key_no));
			printf("key_no : %d\n",key_no);
			switch(key_no) 
			{
				case 1:
            		printf("TIMER STOP! \n");
            		ioctl(dev,TIMER_STOP);
					break;
				case 2: // 키보드로 이동 109라인
            		ioctl(dev,TIMER_STOP);
            		printf("Enter timer value! \n"); 
					break;
				case 3: // 키보드로 이동 117라인
            		ioctl(dev,TIMER_STOP);
            		printf("Enter led value! \n");
					break;
				case 4:
            		printf("TIMER START! \n"); // 커널 타이머 동작
            		ioctl(dev,TIMER_START);
					break;
				case 8:
            		printf("APP CLOSE ! \n");
            		ioctl(dev,TIMER_STOP);
					loopFlag = 0;
				break;

			}
		}
		else if(Events[1].revents & POLLIN) // 키보드 입력 처리
		{
    		fflush(stdin);
			fgets(inputString,sizeof(inputString),stdin);
			if((inputString[0] == 'q') || (inputString[0] == 'Q'))
				break;
			inputString[strlen(inputString)-1] = '\0';
           
			if(key_no == 2) //timer value // 스위치 2번 누르면
			{
				timer_val = atoi(inputString);
				info.timer_val = timer_val;
				ioctl(dev,TIMER_VALUE,&info);
            	ioctl(dev,TIMER_START); // 변경된 값으로 타이머 깜빡
				
			}
			else if(key_no == 3) //led value // 스위치 3번 누르면 0~255 받아서
			{
				led_no = (char)strtoul(inputString,NULL,16);
    			write(dev, &led_no, sizeof(led_no));
            	ioctl(dev,TIMER_START);
			}
			key_no = 0;
		}
	}
	close(dev);
	return 0;
}
