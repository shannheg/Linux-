#ifndef _LED_OPR_H_
#define _LED_OPR_H_

#include <linux/module.h>
#include <linux/types.h>

#define LED_MAX 100

struct led_operation //led的操作函数结构体
{
    struct module *owner;
    int (*init)(int which);//初始化led,选择某一个led
    int (*control)(int which, int status);//控制led的亮灭
};

/* Board-specific LED pin description, passed through platform_data. */
struct board_led_desc {
    unsigned int group;
    unsigned int pin;
    bool active_low;
};

struct board_led_platform_data {
    const struct board_led_desc *leds;
    unsigned int num_leds;
};

int led_device_create(int minor, struct led_operation *opr);
void led_device_destroy(int minor);

int led_init(void);
void led_exit(void);
int chip_init(void);
void chip_exit(void);

#endif
