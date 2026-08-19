#ifdef _LED_RESOURCE_H_
#define _LED_RESOURCE_H_
//使用位域操作，其中bit[31:16]代表寄存器的组，bit[15:0]代表寄存器写入的引脚
struct led_resource
{
    int pin; //led的个数
};

#endif
