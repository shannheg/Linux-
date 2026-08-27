/*
此处是作为板级设置，用于从chip_gpio中获取相关led的组类，并且传递至leddrv.c中注册驱动
注册一个platform_device,用于存储相关的需要初始化的led资源，由name匹配后通过chip_gpio.c的probe进行寄存器的相应操作
*/
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include "led_opr.h"


static struct resource led_resource[] = {
    {
        .pin = 0,
        .flags = IORESOURCE_MEM,
    },
    {
        .pin = 1,
        .flags = IORESOURCE_MEM,
    },
};

static struct platform_device board_demo_led_device = {
    .name = "dp_led",
    .id = -1,
    .num_resources = ARRAY_SIZE(led_resource),
    .resource = led_resource,
};

static int led_dev_init(void)
{
    int err;
    err = platform_device_register(&board_demo_led_device);
    return err;
};
static int led_dev_exit(void)
{
    int err = platform_device_unregister(&board_demo_led_device);
    return err;
};

module_init(led_dev_init);
module_exit(led_dev_exit);

MODULE_LICENSE("GPL");
