#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "led_opr.h"

/*
此处为platform_driver的相关函数，该文件的主要功能为通过name匹配对应的platform_device，并且通过probe函数进行初始化
对于具体向寄存器写入信息的操作，当前由chip_gpio.c中的init和control函数完成，leddrv.c负责调用对应操作函数
*/
static int g_ledpins[LED_MAX]; // LED 使用的 GPIO 组号和引脚号
static bool g_led_active_low[LED_MAX]; // LED 是否为低电平有效
static int g_ledcnt; // 当前已经创建的 LED 节点数量

static void __iomem *gpio_ioc_sel; // iomux 用于选择引脚输出的功能
static void __iomem *gpio_ddr_h; // 输出方向寄存器，设置 output
static void __iomem *gpio_dr_h; // 输出数据寄存器，设置高低电平
static void __iomem *cru_gate_con03; // GPIO1 IOC 时钟
static void __iomem *cru_gate_con22; // GPIO1 时钟

static int chip_gpio_init(int which);
static int chip_gpio_control(int which, int status);

static struct led_operation chip_gpio_operation = {
    .owner = THIS_MODULE,
    .init = chip_gpio_init,
    .control = chip_gpio_control,
};

static void gpio_write_bit(void __iomem *reg, unsigned int pin, int value)
{
    // 写入寄存器：高16位为写掩码，低16位为数据
    writel(BIT(16 + pin) | (value ? BIT(pin) : 0), reg);
}

static void chip_gpio_unmap(void)
{
    /* devm_ioremap() will release the mappings after remove(). */
    gpio_ioc_sel = NULL;
    gpio_ddr_h = NULL;
    gpio_dr_h = NULL;
    cru_gate_con03 = NULL;
    cru_gate_con22 = NULL;
}

/*
 * devm_platform_ioremap_resource_byname()/devm_ioremap_resource() 在映射前会调用
 * request_mem_region() 登记占用区段;而 GPIO1/IOC/CRU 地址已被内核中的
 * GPIO/pinctrl/CRU 驱动(经设备树)占用,继续使用会因冲突返回 -EBUSY。
 * 因此这里只做教学用的直接映射(devm_ioremap),不请求 region,绕开占用检查。
 */
static void __iomem *chip_gpio_ioremap_byname(struct platform_device *pdev,
                                              const char *name)
{
    struct resource *res;

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
    if (!res)
        return NULL;

    return devm_ioremap(&pdev->dev, res->start, resource_size(res));
}

static int chip_gpio_map(struct platform_device *pdev)
{
    void __iomem *gpio_base;
    void __iomem *ioc_base;
    void __iomem *cru_base;

    // GPIO1通用寄存器地址映射与写入(仅映射,不请求内存区段)
    gpio_base = chip_gpio_ioremap_byname(pdev, "gpio1");
    if (!gpio_base)
        return -ENOMEM;

    ioc_base = chip_gpio_ioremap_byname(pdev, "gpio1-ioc");
    if (!ioc_base)
        return -ENOMEM;

    cru_base = chip_gpio_ioremap_byname(pdev, "cru");
    if (!cru_base)
        return -ENOMEM;

    //设置全局需要的寄存器地址
    gpio_dr_h = gpio_base + 0x0004;
    gpio_ddr_h = gpio_base + 0x000c;
    gpio_ioc_sel = ioc_base + 0x0034;
    cru_gate_con03 = cru_base + 0x080c;
    cru_gate_con22 = cru_base + 0x0858;

    writel(BIT(16 + 8), cru_gate_con03);
    writel(BIT(16 + 1), cru_gate_con22);

    /* The original board setup selects GPIO mode for the GPIO1_C pins. */
    writel(0xf0f00000, gpio_ioc_sel);
    return 0;
}

static int chip_gpio_init(int which)
{
    unsigned int group;
    unsigned int pin;

    if (which < 0 || which >= g_ledcnt || !gpio_ddr_h || !gpio_dr_h)
        return -ENODEV;

    group = g_ledpins[which] >> 16;
    pin = g_ledpins[which] & 0xffff;
    if (group != 1 || pin > 7)
        return -EINVAL;

    // 根据当前 LED 编号设置对应 GPIO 为输出并设置初始灭灯状态
    gpio_write_bit(gpio_ddr_h, pin, 1);
    gpio_write_bit(gpio_dr_h, pin, g_led_active_low[which]);
    return 0;
}

static int chip_gpio_control(int which, int status)
{
    unsigned int group;
    unsigned int pin;

    if (which < 0 || which >= g_ledcnt || !gpio_dr_h)
        return -ENODEV;

    group = g_ledpins[which] >> 16;
    pin = g_ledpins[which] & 0xffff;
    if (group != 1 || pin > 7)
        return -EINVAL;

    // 根据当前 LED 编号控制对应 GPIO 的亮灭
    if (g_led_active_low[which])
        status = !status;
    gpio_write_bit(gpio_dr_h, pin, !!status);
    return 0;
}

static int chip_gpio_probe(struct platform_device *pdev)
{
    const struct board_led_platform_data *pdata = dev_get_platdata(&pdev->dev);
    int index;
    int ret;

    if (!pdata || !pdata->leds || !pdata->num_leds ||
        pdata->num_leds > LED_MAX)
        return -EINVAL;

    ret = chip_gpio_map(pdev);
    if (ret)
        return ret;

    //记录board_demo中的led引脚，创建设备节点
    for (index = 0; index < pdata->num_leds; index++) {
        g_ledpins[index] = (pdata->leds[index].group << 16) |
                           pdata->leds[index].pin;
        g_led_active_low[index] = pdata->leds[index].active_low;//把platform_data中的active_low信息传递至全局变量中，便于后续控制led亮灭
        g_ledcnt = index + 1;

        ret = chip_gpio_init(index);
        if (ret)
            goto err_destroy_devices;

        ret = led_device_create(index, &chip_gpio_operation);
        if (ret)
            goto err_destroy_devices;
    }

    return 0;

err_destroy_devices:
    while (--index >= 0) {
        led_device_destroy(index);
        g_ledpins[index] = 0;
        g_led_active_low[index] = false;
    }
    g_ledcnt = 0;
    chip_gpio_unmap();
    return ret;
}

static int chip_gpio_remove(struct platform_device *pdev)
{
    int index;

    //移除相关引脚
    for (index = g_ledcnt - 1; index >= 0; index--) {
        led_device_destroy(index);
        g_ledpins[index] = 0;
        g_led_active_low[index] = false;
    }
    g_ledcnt = 0;
    chip_gpio_unmap();
    return 0;
}

static struct platform_driver chip_gpio_driver = {
    .probe = chip_gpio_probe,
    .remove = chip_gpio_remove,
    .driver = {
        .name = "dp_led",
    },
};

int chip_init(void)
{
    //注册为platform_driver,从chip_gpio中匹配名字，若匹配则进入probe函数
    return platform_driver_register(&chip_gpio_driver);
}

void chip_exit(void)
{
    platform_driver_unregister(&chip_gpio_driver);
}
