/*
此处为platform_driver的相关函数，该文件的主要功能为通过name匹配对应的platform_device，并且通过probe函数进行初始化
对于具体向寄存器写入信息的操作，交由leddrv.c进行操作
*/

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include "led_opr.h"



static int g_ledpins[100];//led总共的数组
static int g_ledcnt = 0;//led编号

static void __iomem *gpio_ioc_sel; //iomux用于选择引脚输出的功能
static void __iomem *gpio_ddr_h;   //输出，设置output
static void __iomem *gpio_dr_h;    //输出置为高电平
static void __iomem *cru_gate_con03;//GPIO1 IOC时钟
static void __iomem *cru_gate_con22;//GPIO1时钟

static int chip_gpio_probe(struct platform_device *dev)
{
    //记录board_demo中的led引脚，创建设备节点
    struct resource *res;
    
    while(1){
        res = platform_get_resource(dev, IORESOURCE_MEM, g_ledcnt);//从platform_device中获取注册为IORESOURCE_MEM的资源
        if(res == NULL){
            break;
        }
        chip_gpio_init(g_ledcnt);//根据当前led编号进行初始化
        g_ledpins[g_ledcnt] = res->pin;//记录当前led的引脚编号
        led_device_create(g_ledcnt);//根据当前led编号创建设备节点
        g_ledcnt++;

    }
    printk("%s %s line %d\n", __FILE__, __func__, __LINE__);
    return 0;
}

static int chip_gpio_remove(struct platform_device *dev)
{
    //移除相关引脚
    iounmap(gpio_ioc_sel);
    iounmap(gpio_ddr_h);
    iounmap(gpio_dr_h);   
    iounmap(cru_gate_con03);
    iounmap(cru_gate_con22);

    for(;g_ledcnt >= 0; g_ledcnt--){
        led_device_destroy(g_ledcnt);
    }
    g_ledcnt = 0;
    
    printk("%s %s line %d\n", __FILE__, __func__, __LINE__);
    return 0;
}

static struct platform_driver chip_gpio = {
    .probe = chip_gpio_probe,
    .remove = chip_gpio_remove,
    .driver = {
        .name = "dp_led",
        .owner = THIS_MODULE,
    },

};

static int chip_gpio_init(int which)//根据当前led编号进行初始化
{
    //GPIO1通用寄存器地址映射与写入
    gpio_ioc_sel  = ioremap(0xFF660000 + 0x0034, 4);
    cru_gate_con03 = ioremap(0xFF9A0000 + 0x080C, 4);
    cru_gate_con22 = ioremap(0xFF9A0000 + 0x0858, 4);

    writel(0x00F00000, gpio_ioc_sel);
    writel(BIT(16 + 8), cru_gate_con03);
    writel(BIT(16 + 1), cru_gate_con22);

    return 0;
    printk("GPIO INIT\n");   
}

static int chip_gpio_control(int which, int status)//根据当前led编号进行控制
{
    switch(which){
        case 0:
            //GPIO1_C5的相应寄存器地址映射
            gpio_ddr_h    = ioremap(0xFF870000 + 0x000C, 4);
            gpio_dr_h     = ioremap(0xFF870000 + 0x0004, 4);

            if(status != 1) break;

            //写入寄存器
            writel(0x00200020, gpio_ddr_h);
            writel(0x00200020, gpio_dr_h); 
            break;
        case 1:
            //GPIO1_C7的相应寄存器地址映射
            gpio_ddr_h    = ioremap(0xFF870000 + 0x000C, 4);
            gpio_dr_h     = ioremap(0xFF870000 + 0x0004, 4);

            if(status != 1) break;

            writel(BIT(7) << 16, gpio_ddr_h);
            writel(BIT(16 + 7), gpio_dr_h);

            break;
        default: return -ENOMEM;
    }
    return 0;
}

struct led_operation chip_gpio_operation = {
    .init = chip_gpio_init,
    .control = chip_gpio_control,
};

struct led_operation *get_board_led_operation(void)
{
    return &chip_gpio_operation;
}


static int chip_init(void)
{
    int ret;
    ret = platform_driver_register(&chip_gpio);//注册为platform_driver,从chip_gpio中匹配名字，若匹配则进入probe函数
    if(ret){
        platform_driver_unregister(&chip_gpio);
        printk("platform_driver_register failed\n");
        return ret;
    }
    return 0;
}

static int chip_exit(void)
{
    int err = platform_driver_unregister(&chip_gpio);
    return err;
}

module_init(chip_init);
module_exit(chip_exit);
MODULE_LICENSE("GPL");