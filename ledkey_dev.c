// sudo mknod /dev/ledkey c 230 0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include "ioctl_test.h"

#define gpioName(a,b) #a#b     //"led""0" == "led0"
#define GPIOLEDCNT 8
#define GPIOKEYCNT 8
#define OFF 0
#define ON 1
#define DEBUG 1

#define   LED_DEV_NAME            "ledkeydev"
#define   LED_DEV_MAJOR            230      

DECLARE_WAIT_QUEUE_HEAD(WaitQueue_Read); // poll blocking

static int timerVal = 100;	// f=100HZ, T=1/100 = 10ms, 100*10ms = 1Sec
module_param(timerVal,int ,0);
static int ledVal = 0;
module_param(ledVal,int ,0);

/* int */
static int sw_irq[8] = {0};

/* tim */
static struct timer_list timerLed;
static void kerneltimer_func(struct timer_list *t );
static void kerneltimer_registertimer(unsigned long timeover);

/* GPIO pin */
static int gpioLed[GPIOLEDCNT] = {6,7,8,9,10,11,12,13};
static int gpioKey[GPIOKEYCNT] = {16,17,18,19,20,21,22,23};

/* GPIO */
static int gpioLedInit(void);
static void gpioLedSet(long);
static void gpioLedFree(void);
static int gpioKeyInit(void);
static void gpioKeyFree(void);

/* poll */
static unsigned int ledkeydev_poll(struct file * filp, struct poll_table_struct * wait);

static int gpioLedInit(void)
{
	int ret = 0;
	for(int i = 0; i < GPIOLEDCNT; i++)
	{
		ret = gpio_request(gpioLed[i], gpioName(led,i));
		if(ret < 0) {
			printk("Failed Request gpio%d error\n", i);
			return ret;
		}
	}
	for(int i = 0; i < GPIOLEDCNT; i++)
	{
		ret = gpio_direction_output(gpioLed[i], OFF);
		if(ret < 0) {
			printk("Failed direction_output gpio%d error\n", i);
       	 return ret;
		}
	}
	return ret;
}

static void gpioLedSet(long val) 
{
	for(int i = 0; i < GPIOLEDCNT; i++)
	{
		gpio_set_value(gpioLed[i], (val>>i) & 0x01);
	}
}

static void gpioLedFree(void)
{
	for(int i = 0; i < GPIOLEDCNT; i++)
	{
		gpio_free(gpioLed[i]);
	}
}

static int gpioKeyInit(void) 
{
	int ret=0;
	for(int i = 0; i < GPIOKEYCNT; i++)
	{
		ret = gpio_request(gpioKey[i], gpioName(key,i));
		if(ret < 0) {
			printk("Failed Request gpio%d error\n", i);
			return ret;
		}
	}
	for(int i = 0; i < GPIOKEYCNT; i++)
	{
		ret = gpio_direction_input(gpioKey[i]);
		if(ret < 0) {
			printk("Failed direction_output gpio%d error\n", i);
       	 return ret;
		}
	}
	return ret;
}

static void gpioKeyFree(void) 
{
	for(int i = 0; i < GPIOKEYCNT; i++)
	{
		gpio_free(gpioKey[i]);
	}
}

/* int */
static void gpioKeyToIrq(void)
{
	int i;
    for (i = 0; i < GPIOKEYCNT; i++) {
        // sw_irq[0] = 16
        // sw_irq[7] = 23 
        sw_irq[i] = gpio_to_irq(gpioKey[i]); // gpio 번호를 irq 번호로 바꿈 // /proc/interrupts
	}
}

static void gpioKeyFreeIrq(struct file *filp)
{
	int i;
	for (i = 0; i < GPIOKEYCNT; i++){
        free_irq(sw_irq[i], filp->private_data); // request_irq 시 전달한 메모리 공간도 해제, (디바이스 id, 메모리) 
	}
}

// 커널이 처리하는 함수라 (이 파일 외부에서 처리하는 것) static 없어야 함 
//irqreturn_t sw_isr(int irq, void *unuse) // unuse로 공유할 수 있다. // 인터럽트 서비스 루틴과 ps간에 매개변수 공유 
irqreturn_t sw_isr(int irq, void *private_data) // private_data : kmalloc 으로 할당한 메모리 주소가 넘어옴, request_irq 마지막 인자로 넣은 값임
{
    char *pSw_no = (char *)private_data; // 크기를 부여(형변환)해서 대입
    
    // request_irq() 함수의 마지막 인자로 공유할(여기선 unuse) 매개변수를 등록
	for(int i = 0; i < GPIOKEYCNT; i++)
	{
		if(irq == sw_irq[i]) // sw_irq[0] ~ [7] => 16~23
		{
            *pSw_no = i + 1;
			break;
		}
	}
	printk("IRQ number : %d, key(switch)_number : %d\n", irq, *pSw_no);
	return IRQ_HANDLED;
}

// 인터럽트 등록 // 다른 시스콜과 공유하기 위해 init에서 여기로 옮김
static int requestIrqInit(struct file *filp)
{
    int result = 0;
    // irq
	for(int i = 0; i < GPIOKEYCNT; i++)
	{
        // 인터럽트 등록 
        // 16~23을 넣어 sw_isr() 호출
        // 키 8개가 모두 sw_isr 함수를 호출함 - 인터럽트 발생 시킨 키 번호 출력하는 함수 
        // IRQF_TRIGGER_RISING : 키 눌릴때 인터럽트(누르면 high) 트리거 - 라이징 엣지에 인터럽트 발생 
		//result = request_irq(sw_irq[i], sw_isr, IRQF_TRIGGER_RISING, gpioName(key,i), NULL);
        result = request_irq(sw_irq[i], sw_isr, IRQF_TRIGGER_RISING, gpioName(key,i), filp->private_data);
		if(result)
		{
			printk("#### FAILED Request irq %d. error : %d \n", sw_irq[i], result);
			break;
		}
	}
    
    return 0;
}


static int ledkeydev_open(struct inode *inode, struct file *filp)
{
    char *pSw_no = NULL; // 4byte 자료형이지만 1byte 공간을 가리킴 // 변수 선언을 printk 보다 아래에 하면 워닝
    
    //int *pTimerVal = NULL;
    //int *pLedVal = NULL;
    
    int result = 0;
    
    int num0 = MAJOR(inode->i_rdev); 
    int num1 = MINOR(inode->i_rdev); 
    printk( "ledkeydev open -> major : %d\n", num0 );
    printk( "ledkeydev open -> minor : %d\n", num1 );
    
    // 6~13 init
	result = gpioLedInit(); 
	if(result < 0)
  		return result;     /* Device or resource busy */

	result = gpioKeyInit();
	if(result < 0)
  		return result;     /* Device or resource busy */
    
	gpioKeyToIrq();
    
    // 할당된 주소 반환
    pSw_no = kmalloc(sizeof(char), GFP_KERNEL); // 할당할 size, 할당 될때까지 슬립 or 할당 안되면 NULL반환
    if(pSw_no == NULL) {
        return -ENOMEM; // 메모리 부족
    }

    // 모든 시스콜이 매개변수로 받는 file 구조체 내
    // void *private_data로 공유함(시스콜간 공유)
    filp->private_data = pSw_no; // private_data 은 void 포인터이므로 꺼내쓸땐 (char *)로 형변환 필요
    
    // 인터럽트 등록
    requestIrqInit(filp); // 넘겨준 private_data를 인터럽트 등록 시 마지막 인자로 사용

    return 0;
}

static ssize_t ledkeydev_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    char *pSw_no = filp->private_data;
	int ret;
    ret = copy_to_user(buf, pSw_no, count);
    //printk("read *pSw_no : %d\n", *pSw_no);
	*pSw_no = 0;
    
	if(ret < 0)
		return -ENOMEM;
    return count;
}

static ssize_t ledkeydev_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	int ret;
	ret = copy_from_user(&ledVal, buf, count);
	if(ret < 0)
		return -ENOMEM; // 메모리 부족 
    
	//ledVal = 1 << (ledVal - 1); // 8 => 128
    printk("_write ledVal %x\n", ledVal);
    
    return count;
}

static long ledkeydev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	keyled_data ctrl_info = {0};
    
	int err = 0;
    int size = 0;
	if( _IOC_TYPE( cmd ) != IOCTLTEST_MAGIC ) return -EINVAL;
	if( _IOC_NR( cmd ) >= IOCTLTEST_MAXNR ) return -EINVAL;

	size = _IOC_SIZE( cmd );
	if( size )
	{
		err = 0;
		if( _IOC_DIR( cmd ) & _IOC_READ )
			err = access_ok( (void *) arg, size );
		if( _IOC_DIR( cmd ) & _IOC_WRITE )
			err = access_ok( (void *) arg, size );
		if( !err ) return err;
	}
	switch( cmd )
	{
        case TIMER_START:
            kerneltimer_registertimer(timerVal); // init tim
            break;
        case TIMER_STOP:
            if(timer_pending(&timerLed)) del_timer(&timerLed); // del tim
            break;
        case TIMER_VALUE: // led on/off 주기 
            err = copy_from_user((void *)&ctrl_info,(void *)arg,(unsigned long)sizeof(ctrl_info));
            timerVal = ctrl_info.timer_val;
            printk("ioctl TIMER_VALUE after timerVal : %d\n", timerVal);
            break;
        
        /*
		read
			err = copy_to_user((void *)arg, (const void *)&ctrl_info, (unsigned long)sizeof(ctrl_info));
		write
			err = copy_from_user((void *)&ctrl_info, (void *)arg, (unsigned long)sizeof(ctrl_info));
        */

		default:
			err =-E2BIG;
			break;
	}	
	return err;
}

static int ledkeydev_release (struct inode *inode, struct file *filp)
{
    // ctrl + c
    printk("ledkeydev release \n");
    
    gpioLedSet(0);
	gpioLedFree();
    gpioKeyFreeIrq(filp);
    gpioKeyFree();
    if(timer_pending(&timerLed)) del_timer(&timerLed); // del tim
    
    // kmalloc kfree
    if(filp->private_data != NULL)
        kfree(filp->private_data);
    
    return 0;
}

/* tim */
static void kerneltimer_registertimer(unsigned long timeover)
{
	timer_setup( &timerLed,kerneltimer_func,0);
	timerLed.expires = get_jiffies_64() + timeover;  //10ms *100 = 1sec
	add_timer( &timerLed );
}

// 타이머 핸들러
static void kerneltimer_func(struct timer_list *t) // 등록된 타이머가 인자로 들어옴
{
    gpioLedSet(ledVal);
    ledVal = ~ledVal & 0xff;

    // 만료 시간만 갱신
    mod_timer(t, get_jiffies_64() + timerVal); // 현재 기준 + 또 100번 카운트(1초 카운트)
}


/* poll */
// 커널을 통해 인자를 받아옴 (filp, wait)
static unsigned int ledkeydev_poll(struct file * filp, struct poll_table_struct * wait)
{
    char *pSw_no = filp->private_data;
	unsigned int mask = 0;
    
	if(wait->_key & POLLIN) // 이벤트 종류 읽기
		poll_wait(filp, &WaitQueue_Read, wait); // 블로킹 함수(잠재움)
        //  wait 안에 타임아웃이 저장됨, 타임아웃에 따라 깨어남
	if(*pSw_no > 0) // 타임아웃 전에 키 입력이 있으면 
		mask = POLLIN;
	return mask; // revents 에 담아 커널이 return 해줌 // 타임아웃이면 0 리턴 // 입력이 들어오면 POLLIN return
}

static struct file_operations ledkeydev_fops =
{
    .owner          = THIS_MODULE,
    .open           = ledkeydev_open,     
    .read           = ledkeydev_read,     
    .write          = ledkeydev_write,    
	.unlocked_ioctl = ledkeydev_ioctl,
    .poll	        = ledkeydev_poll,
    .release        = ledkeydev_release,  
};

static int ledkey_init(void)
{
    int result=0;
    printk( "ledkeydev ledkeydev_init \n" );    
    
    #if DEBUG
        printk("timerVal : %d , sec : %d \n",timerVal,timerVal/HZ );
    #endif
    
    // dev 등록 
    result = register_chrdev(LED_DEV_MAJOR, LED_DEV_NAME, &ledkeydev_fops);
    if (result < 0) return result;
	
    return result;
}

static void ledkey_exit(void)
{
    printk(" ledkeydev_exit \n");    
    unregister_chrdev(LED_DEV_MAJOR, LED_DEV_NAME);    
}

module_init(ledkey_init);
module_exit(ledkey_exit);

MODULE_AUTHOR("KCCI-AIOT Kim Jeong Kyun");
MODULE_DESCRIPTION("led key test module");
MODULE_LICENSE("Dual BSD/GPL");
