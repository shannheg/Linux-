#include <linux/gfp.h>
#include "led_opr.h"
#include "led_resource.h"

static volatile void __iomem *gpio1_dr_l;
static volatile void __iomem *gpio1_ddr_h;
static volatile void __iomem *gpio1_ioc_sel;
static volatile void __iomem *cru_gate_con03;
static volatile void __iomem *cru_gate_con22;

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

static struct led_resource board_demo_led_opr = {
    .pin  = (5 << 16) | (5 << 0), //bit[31:16]代表寄存器的组，bit[15:0]代表寄存器写入的引脚;
};

struct led_resource *get_board_led_resource(void)
{
    return &board_demo_led_opr;
}

