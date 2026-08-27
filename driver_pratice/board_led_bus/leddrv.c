#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include "led_opr.h"
static int major;

static struct led_operation *chip_gpio_operation;
static struct class *led_class;

static void __iomem *gpio1_ioc_sel;
static void __iomem *gpio1_ddr_h;//输出，设置output
static void __iomem *gpio1_dr_h;//置为1
static void __iomem *cru_gate_con03;
static void __iomem *cru_gate_con22;


static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos){
    char val;//用于将用户空间读取的值暂存
    int minor;//从本次打开的字符设备获取设备号
    int status;//本次led的状态

    if(count < 1){
        return -EINVAL;
    }

    if(copy_from_user(&val, buf, 1)){//从用户空间读取1个数据到val中
        return -EFAULT;
    }
    if(val == '1'){
        status = 1;
    }
    else{
        status = 0;
    }
    minor = iminor(file->f_inode);//获取当前打开的设备号
    int ret = chip_gpio_operation->control(minor, status);
    if(ret){
        return ret;
    }
    return 1;
}

static int led_open(struct inode *node, struct file *file){
    int minor = iminor(node);
    chip_gpio_operation->init(minor);
    return 0;
}

static struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .write = led_write,
    .open = led_open,
    
};


//入口函数
static int __init led_init(void){
    printk("%s, %s, line %d \n ------------------make by dp\n", __FILE__, __func__, __LINE__);
    major = register_chrdev(0, "dp_led", &led_fops);//注册字符设备的主设备号，将设备号范围与file_operations结构体绑定
    if(major < 0){
        printk("register_chrdev failed\n");
        return major;
    }

    led_class = class_create(THIS_MODULE, "my_led");//把同一类相似的设备号组织在一起，为后续device_create创建设备节点提供支持,设备中显示为/sys/class/my_led/
    if(IS_ERR(led_class)){
        unregister_chrdev(major, "dp_led");
        return PTR_ERR(led_class);
    }

    chip_gpio_operation = get_board_led_operation();//获取led的操作函数结构体

    return 0;
}

void led_device_create(int minor){
    device_create(led_class, NULL, MKDEV(major,minor), NULL, "leddrv-dpled%d", minor);//创建具体的设备节点，这里通过此设备号创建不同的设备

}

void led_device_destroy(int minor){
    device_destroy(led_class, MKDEV(major, minor));
}
EXPORT_SYMBOL(led_device_create);
EXPORT_SYMBOL(led_device_destroy);

static void __exit led_exit(void){
    class_destroy(led_class); 
    unregister_chrdev(major, "dp_led");
}
module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
