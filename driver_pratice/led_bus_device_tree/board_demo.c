#include <linux/module.h>
#include <linux/platform_device.h>

#include "led_opr.h"

/*
此处是作为板级设置，用于从chip_gpio中获取相关led的组类，并且传递至leddrv.c中注册驱动
注册一个platform_device,用于存储相关的需要初始化的led资源，由name匹配后通过chip_gpio.c的probe进行寄存器的相应操作
*/
/*
 * resource 保存需要被映射的真实寄存器物理地址范围。
 * LED 使用哪一个 GPIO 引脚由 platform_data 的 board_leds[] 描述。
 */
/*
static struct resource led_resources[] = {
    {
        .start = 0xFF870000,
        .end = 0xFF8700FF,
        .flags = IORESOURCE_MEM,
        .name = "gpio1",
    },
    {
        .start = 0xFF660000,
        .end = 0xFF6600FF,
        .flags = IORESOURCE_MEM,
        .name = "gpio1-ioc",
    },
    {
        .start = 0xFF9A0000,
        .end = 0xFF9A08FF,
        .flags = IORESOURCE_MEM,
        .name = "cru",
    },
};

static const struct board_led_desc board_leds[] = {
    {
        .group = 1,
        .pin = 5,
        .active_low = false,
    },
    {
        .group = 1,
        .pin = 7,
        .active_low = false,
    },
};

static struct board_led_platform_data board_led_data = {
    .leds = board_leds,
    .num_leds = ARRAY_SIZE(board_leds),
};

static struct platform_device board_demo_led_device = {
    .name = "dp_led",
    .id = -1,
    .num_resources = ARRAY_SIZE(led_resources),
    .resource = led_resources,
    .dev = {
        .platform_data = &board_led_data,
    },
};
*/
static int __init board_demo_init(void)
{
    int err;

    // platform_device 由设备树自动创建，这里只注册字符设备和 platform_driver
    err = led_init();
    if (err)
        return err;

    err = chip_init();
    if (err) {
        led_exit();
        return err;
    }

    return 0;
}

static void __exit board_demo_exit(void)
{
    chip_exit();
    led_exit();
}

module_init(board_demo_init);
module_exit(board_demo_exit);

MODULE_LICENSE("GPL");
