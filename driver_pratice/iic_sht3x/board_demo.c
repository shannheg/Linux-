#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include "sht3x.h"
//iic scl:gpio1_d1
//iic sda:gpio0_b0
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
