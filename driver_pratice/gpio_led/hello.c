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

static int major;

static struct class *led_class;

static void __iomem *gpio1_ioc_sel;
static void __iomem *gpio1_ddr_h;
static void __iomem *gpio1_dr_h;
static void __iomem *cru_gate_con03;
static void __iomem *cru_gate_con22;

static ssize_t led_write(struct file *file, const char *buf, size_t count, loff_t *ppos){
    char val;
    val = copy_from_user(buf, &val, 1);
    if(val == 1){
        writel(0x00200020, gpio1_dr_h);
    }
    else{
        writel(0x00200020, gpio1_dr_h);
    }
    return 1;
}

static int led_open(struct inode *node, struct file *file){
    writel(BIT(16 + 8), cru_gate_con03);
    writel(BIT(16 + 1), cru_gate_con22);
    writel(0x00F00000, gpio1_ioc_sel);
    writel(0x00200020, gpio1_ddr_h);
    writel(0x00200020, gpio1_dr_h); 
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
    major = register_chrdev(0, "dp_led", &led_fops);

    gpio1_ioc_sel  = ioremap(0xFF660000 + 0x0034, 4);
    gpio1_ddr_h    = ioremap(0xFF870000 + 0x000C, 4);
    gpio1_dr_h     = ioremap(0xFF870000 + 0x0004, 4);
    cru_gate_con03 = ioremap(0xFF9A0000 + 0x080C, 4);
    cru_gate_con22 = ioremap(0xFF9A0000 + 0x0858, 4);

    //ioremap
    led_class = class_create(THIS_MODULE, "my_led");
    device_create(led_class, NULL, MKDEV(major, 0), NULL, "my_led");

    return 0;
}

static void __exit led_exit(void){
    device_destroy(led_class, MKDEV(major, 0));
    class_destroy(led_class); 
    unregister_chrdev(major, "dp_led");
}
module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
