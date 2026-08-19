#include <linux/gfp.h>
#include "led_opr.h"

static int board_demo_led_init (int which)
{
    printk("%s %s line %d, led %d\n---dpboard_demo_init", __FILE__, __func__, __LINE__, which);
    return 0;
}

static int board_demo_led_control (int which, int status)
{
    printk("%s %s line %d, led %d status = %s\n---dpboard_demo_control", __FILE__, __func__, __LINE__, which, status ? "ON" : "OFF");
    return 0;
}

static struct led_operation board_demo_led_opr = {
    .init = board_demo_led_init,
    .control = board_demo_led_control,
};

struct led_operation *get_board_led_operation(void)
{
    return &board_demo_led_opr;
}
