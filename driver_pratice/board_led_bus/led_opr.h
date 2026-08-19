#ifndef _LEF_OPR_H
#define _LEF_OPR_H

struct led_operation
{
    int (*init) (int which);//初始化led,选择某一个led
    int (*control) (int which, int status);//控制led的亮灭
};

struct led_operation *get_board_led_operation(void);
#endif