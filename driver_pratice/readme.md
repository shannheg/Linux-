board_led:
最简单的一个驱动控制led,在leddrv中完成寄存器地址写入以控制led

----------------------------------------------------------------------------------------------
board_led_bus:
文件用途:
board_demo.c: platform_device存放的文件，包括platform_device的注册以及驱动入口初始化，还包括寄存器资源的存放
led_opr.h：led操作函数存放文件，包括led的init和control函数
chip_gpio.c: platform_device存放与注册的文件
leddrv.c：实际led进行写入寄存器函数的文件

这是一个platfrom总线模型的LED测试驱动程序，整个的运行流程如下:
在加载时，insmod test_drv.ko,运行./dpdrv 0 1，即可控制第一个LED为高电平，驱动的运行流程如下
进入board_demo_init,首先进入led_init,此函数用于注册字符型的设备register_chrdev，包括分配主设备号、创建相同类(sys/class下可见)，为创建设备节点打基础的class_create，以及最后通过主设备号分配的次设备号创建的设备节点
随后加载注册platform_device和platform_driver,这两个驱动在加载的时候通过结构体下的name进行匹配，如果匹配成功则进入各自的init函数，platfrom_driver则进入probe函数进行初始化
各个文件的作用：
board_demo.c：作为platfrom_device加载的文件
chip_gpio.c: 作为platfrom_driver的文件，还包括GPIO的寄存器初始化以及设定等
leddrv.c: 底层驱动，用于注册字符设备、设备节点等，还包括从用户层读取设备后通过led_opeartion操作函数转入chip_gpio的control中设置为高电平输出

----------------------------------------------------------------------------------------------
led_bus_device_tree:
这是一个将设备中描述并在启动的时候注册的platform_device用于替换在board_demo.c中注册的设备，相对应的驱动也有少许改动
chip_gpio.c：platform_driver 用于注册led的驱动
dp-rk3506g2:总的设备树，编译的时候需放在kernel/arch/arm/boot/dts下
vanxoak_practice_rk3506b_defconfig:板级配置，存放目录为device/rockchip/.chips/rk3506,编译前使用./build.sh lunch指定
Kbuild：使得驱动编译进内核的文件,如果想单独编译驱动的话，则需屏蔽此文件，编译完再insmod加载即可
加载流程：内核解析设备树，创建platform_device,可在/sys/device/platform中查询到，随后根据设备的compatible属性与驱动.of_match_table中的compatible匹配，匹配成功则创建设备节点，当前工程创建的节点位于/dev目录下。

设备树语法：dp_led@ff870000位于根节点下，为led@0与led@1的parent节点，其中@ff870000是只用于内核读取的单元地址，保证内核在命名的时候具有唯一性。
compatible是真正用于创建设备节点的命名，其中，前面为驱动公司的名字，后面为驱动的名字
reg为所需的寄存器，如在此设备树中需要三个寄存器，<0xff870000 0x100>中0xff870000代表寄存器的基地址，0x100代表长度
#address-cells = <1>;#size-cells = <0>;这两句是用于描述父节点(dp_led)下的子节点(leds)中reg数据是如何读取的，其中address-cells代表reg中的数据有几个32位的字段，size-cells代表地址后面有几个32位的字段，故此处reg = <0>只有一个32位的字段

驱动中读取设备树的函数：
static inline struct device_node *of_get_child_by_name(const struct device_node *node,const char *name)从父节点node下读取名为name的子节点，并返回一个device_node的结构体
static inline int of_property_read_u32(const struct device_node *np,const char *propname,u32 *out_value)从节点中读取数据，np为节点，propname为设备树内名为"..."的数据，存入out_value中
#define for_each_child_of_node(parent, child)从父节点下依次读取子节点，parent为父节点，child为读取的子节点

