#ifndef _LEF_OPR_H
#define _LEF_OPR_H

struct led_operation //led的操作函数结构体
{
    int (*init) (int which);//初始化led,选择某一个led
    int (*control) (int which, int status);//控制led的亮灭
};

struct led_operation *get_board_led_operation(void);//获取led的操作函数结构体
#endif