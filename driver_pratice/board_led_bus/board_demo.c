/*
此处是作为板级设置，用于从chip_gpio中获取相关led的组类，并且传递至leddrv.c中注册驱动
*/
#include "led_resource.h"

static int g_ledpins[100];
static int g_ledcnt = 0;

static struct resource led_resource[] = {
    {
        .start = GROUP_PIN(1, 5),
        .flags = IORESOURCE_MEM1,
    },
    {
        .start = GROUP_PIN(1, 6),
        .flags = IORESOURCE_MEM2,
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
    return 0;
};
static void led_dev_exit(void)
{
    platform_device_unregister(&board_demo_led_device);
    return 0;
};

module_init(led_dev_init);
module_exit(led_dev_exit);

module_license("GPL");
