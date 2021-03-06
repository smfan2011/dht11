/***************************** 
*
*   驱动程序模板
*   版本：V1
*   使用方法(末行模式下)：
*   :%s/dht11/"你的驱动名称"/g
*
*******************************/


#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/raw.h>
#include <linux/tty.h>
#include <linux/capability.h>
#include <linux/ptrace.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/crash_dump.h>
#include <linux/backing-dev.h>
#include <linux/bootmem.h>
#include <linux/splice.h>
#include <linux/pfn.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/aio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/ioctl.h>

//配置连接温度传感器的引脚
#define DHT11_L	   	        *GPIO39_DATA &= ~(1<<7)  //低电平	
#define DHT11_H		        *GPIO39_DATA |=  (1<<7) //高电平
#define DHT11_OUT	        *GPIO39_DIR  |=  (1<<7)  //输出	
#define DHT11_IN		    *GPIO39_DIR  &= ~(1<<7)  //输入	
#define DHT11_STA		    ((*GPIO39_DATA>>7) & 0x01)	

//寄存器定义
volatile unsigned long *GPIO39_MODE;
volatile unsigned long *GPIO39_DIR;
volatile unsigned long *GPIO39_DATA;	


/****************  基本定义 **********************/
//初始化函数必要资源定义
//用于初始化函数当中
//device number;
	dev_t dev_num;
//struct dev
	struct cdev dht11_cdev;
//auto "mknode /dev/dht11 c dev_num minor_num"
struct class *dht11_class = NULL;
struct device *dht11_device = NULL;

/********************  dht11有关的函数   ****************************/
//从dht11中读取一个字节
static unsigned char read_byte(void)
{
	unsigned char r_val = 0; 
	unsigned char t_count = 0; //计时器，防止超时；
        unsigned char i;

	for(i = 0 ; i < 8 ; i++)
	{
		t_count = 0;
		
		while(!DHT11_STA)
		{
			udelay(1);
			t_count++;
			if(t_count>250)
			{
				printk("read_byte error1\n");
				return 100;
			}
		}
		t_count = 0;

		udelay(32);

		if(DHT11_STA == 1)
		{
			r_val <<= 1;
			r_val |= 1;
		}
		else
		{
			r_val <<= 1;
			continue;
		}

		while( DHT11_STA == 1 )
		{
			udelay(2);
			t_count++;
			if(t_count>250)
			{
				printk("read_byte error2\n");
				return 100;
			}
	 	 }
	}

	return r_val;
}

//从dht11中读出数据
static unsigned int read_dht11(void)
{
	 unsigned char t_count = 0; //计时器；
	 unsigned int  dht11 = 0;
	 unsigned char h_i = 0 , h_f = 0;
	 unsigned char t_i = 0 , t_f = 0;
	 unsigned char check_sum = 0;

	 DHT11_OUT;

	 DHT11_L;
	 mdelay(30); //>18ms;
	 DHT11_H;

	 udelay(30);

	 DHT11_IN;
	 while(DHT11_STA == 1)
	 {
	 	udelay(1);
		t_count++;

		if(t_count > 50)
		{
	 		printk("device error: dht11!\n");
			return 0;
		}
	 }
	 t_count = 0;

 	 while(!DHT11_STA)
	 {
		udelay(1);
		t_count++;

		if(t_count > 250)
		{
			printk("read_dht11 error1\n");
			return 0;
		}
	 }

	 t_count = 0;

	 udelay(50);

	 while(DHT11_STA)
	 {
		udelay(1);
		t_count++;
		if(t_count > 250)
		{
			printk("read_dht11 error2\n");
			return 0;
		}
	 }

	 h_i = read_byte();
	 h_f = read_byte();
	 t_i = read_byte();
	 t_f = read_byte();
	 check_sum = read_byte();

	 if(check_sum == (h_i+h_f+t_i+t_f) || (h_i!=100 && t_i != 100))
	 {
		dht11 = t_i;
		dht11 <<= 8;
		dht11 += h_i;
	 }
	 else
	 {
		dht11 = 0;
		printk("read_dht11 error3\n");
	 }

	 return dht11;
}


/**********************************************************************/

/**************** 结构体 file_operations 成员函数 *****************/
//open
static int dht11_open(struct inode *inode, struct file *file)
{
	printk("dht11 drive open...\n");

	DHT11_OUT;
	DHT11_H;

	return 0;
}

//close
static int dht11_close(struct inode *inode , struct file *file)
{
	return 0;
}

//read
static ssize_t dht11_read(struct file *file, char __user *buffer,
			size_t len, loff_t *pos)
{
	unsigned int dht11; 
	printk("dht11 drive read...\n");

	dht11 = read_dht11();
	copy_to_user(buffer, &dht11, 4);

	return 4;
}



/***************** 结构体： file_operations ************************/
//struct
static const struct file_operations dht11_fops = {
	.owner   = THIS_MODULE,
	.open	 = dht11_open,
	.release = dht11_close,	
	.read	 = dht11_read,
};


/*************  functions: init , exit*******************/
//条件值变量，用于指示资源是否正常使用
unsigned char init_flag = 0;
unsigned char add_code_flag = 0;

//init
static __init int dht11_init(void)
{
	int ret_v = 0;
	printk("dht11 drive init...\n");

	//函数alloc_chrdev_region主要参数说明：
	//参数2： 次设备号
	//参数3： 创建多少个设备
	if( ( ret_v = alloc_chrdev_region(&dev_num,0,1,"dht11") ) < 0 )
	{
		goto dev_reg_error;
	}
	init_flag = 1; //标示设备创建成功；

	printk("The drive info of dht11:\nmajor: %d\nminor: %d\n",
		MAJOR(dev_num),MINOR(dev_num));

	cdev_init(&dht11_cdev,&dht11_fops);
	if( (ret_v = cdev_add(&dht11_cdev,dev_num,1)) != 0 )
	{
		goto cdev_add_error;
	}

	dht11_class = class_create(THIS_MODULE,"dht11");
	if( IS_ERR(dht11_class) )
	{
		goto class_c_error;
	}

	dht11_device = device_create(dht11_class,NULL,dev_num,NULL,"dht11");
	if( IS_ERR(dht11_device) )
	{
		goto device_c_error;
	}
	printk("auto mknod success!\n");

	//------------   请在此添加您的初始化程序  --------------//
       

	GPIO39_MODE = (volatile unsigned long *)ioremap(0x10000064, 4);
	GPIO39_DATA = (volatile unsigned long *)ioremap(0x10000624, 4);
	GPIO39_DIR =  (volatile unsigned long *)ioremap(0x10000604, 4);

        //如果需要做错误处理，请：goto dht11_error;	
    *GPIO39_MODE |= (1u<<10);
    *GPIO39_DIR  |=  (1u<<7);
	 add_code_flag = 1;
	//----------------------  END  ---------------------------// 

	goto init_success;

dev_reg_error:
	printk("alloc_chrdev_region failed\n");	
	return ret_v;

cdev_add_error:
	printk("cdev_add failed\n");
 	unregister_chrdev_region(dev_num, 1);
	init_flag = 0;
	return ret_v;

class_c_error:
	printk("class_create failed\n");
	cdev_del(&dht11_cdev);
 	unregister_chrdev_region(dev_num, 1);
	init_flag = 0;
	return PTR_ERR(dht11_class);

device_c_error:
	printk("device_create failed\n");
	cdev_del(&dht11_cdev);
 	unregister_chrdev_region(dev_num, 1);
	class_destroy(dht11_class);
	init_flag = 0;
	return PTR_ERR(dht11_device);

//------------------ 请在此添加您的错误处理内容 ----------------//
dht11_error:
		



	add_code_flag = 0;
	return -1;
//--------------------          END         -------------------//
    
init_success:
	printk("dht11 init success!\n");
	return 0;
}

//exit
static __exit void dht11_exit(void)
{
	printk("dht11 drive exit...\n");	

	if(add_code_flag == 1)
 	{   
           //----------   请在这里释放您的程序占有的资源   ---------//
	    printk("free your resources...\n");	               

		iounmap(GPIO39_MODE);      
		iounmap(GPIO39_DATA);
		iounmap(GPIO39_DIR);

	    printk("free finish\n");		               
	    //----------------------     END      -------------------//
	}					            

	if(init_flag == 1)
	{
		//释放初始化使用到的资源;
		cdev_del(&dht11_cdev);
 		unregister_chrdev_region(dev_num, 1);
		device_unregister(dht11_device);
		class_destroy(dht11_class);
	}
}


/**************** module operations**********************/
//module loading
module_init(dht11_init);
module_exit(dht11_exit);

//some infomation
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("from Jafy");
MODULE_DESCRIPTION("dht11 drive");



/*********************  The End ***************************/
