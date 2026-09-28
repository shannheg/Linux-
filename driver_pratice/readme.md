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

----------------------------------------------------------------------------------------------------------
gpio_interrupt:
这是一个gpio中断实验的示例，采用gpio3_a7输入gpio中断
设备树：dp-rk3506g-mini.dts:在pinctrl中定义真正需要引用的节点，包括gpio3_a7输入
中断读取：
led注册与使用：注册为ledclass

实验现象：按一下按键（GPIO3_A7，下降沿中断），中断线程里把 LED（GPIO3_A6，低电平点亮）翻转一次。

一、文件用途
interrupt-key-led.c：LED 驱动（platform_driver，compatible = "dp_industry,dp_led"）
    从设备树的 led-gpios 取得 GPIO，注册成 led_classdev（名字 dp-led），
    注册成功后用户层可通过 /sys/class/leds/dp-led/brightness 控制。
interrupt_key.c：按键中断驱动（platform_driver，compatible = "dp_industry,dp_key"）
    probe 里通过 dts 的 leds = <&dp_led> 取到上面那个 led_classdev，再注册中断，
    中断线程里翻转 LED。
Kbuild：编入内核用（obj-y += interrupt-key-led.o / obj-y += interrupt_key.o）
Makefile：单独编译成模块用（obj-m）；kbuild 优先读 Kbuild，编模块前先把 Kbuild 改名屏蔽。
dp_rk3506_g_mini_defconfig：板级配置，放到 device/rockchip/.chips/rk3506，编译前用 ./build.sh lunch 指定。

二、设备树要点
1. 两个节点都挂在根节点下：
   dp-gpio-interrupt {
       compatible    = "dp_industry,dp_key";
       pinctrl-names = "default";
       pinctrl-0     = <&key_gpio3_a7>;                  // 复用成 GPIO 功能，带上拉
       interrupt-parent = <&gpio3>;
       interrupts    = <RK_PA7 IRQ_TYPE_EDGE_FALLING>;   // GPIO3_A7 下降沿触发
       leds          = <&dp_led>;                        // 关键：指向 LED 节点，按键驱动靠它找 LED
       status        = "okay";
   };
   dp-led {
       compatible    = "dp_industry,dp_led";
       pinctrl-0     = <&dp_gpio_led>;
       led-gpios     = <&gpio3 RK_PA6 GPIO_ACTIVE_LOW>;  // con_id 为 "led"，对应驱动 devm_gpiod_get(dev, "led", ...)
       status        = "okay";
   };

2. pinctrl 必须写成两层：功能(function)节点 -> 组(group)节点（组里才有 rockchip,pins）：
   &pinctrl {
       led   { dp_gpio_led:  dp_gpio_led  { rockchip,pins = <3 RK_PA6 RK_FUNC_GPIO &pcfg_pull_none>; }; };
       key-1 { key_gpio3_a7: key_gpio3_a7 { rockchip,pins = <3 RK_PA7 RK_FUNC_GPIO &pcfg_pull_up>; }; };
   };
   组若直接平铺在 &pinctrl 下，会被驱动当成"没有子节点的 function"，一个 group 都不注册，
   引用它的设备会在 probe 之前就报 "unable to find group for node xxx" 而 probe 失败（本实验踩过）。
   （文件里还有早期用 GPIO4_B2 的组 key_gpio4_b2，节点已改用 key_gpio3_a7。）

3. GPIO_ACTIVE_LOW：led-gpios 声明低有效，驱动里写逻辑值 1 即点亮（引脚输出低电平）。

三、驱动流程
LED 侧（interrupt-key-led.c）：
  compatible 匹配 -> chip_gpio_probe()
  -> devm_gpiod_get(dev, "led", GPIOD_OUT_LOW)      取 GPIO（低有效，初始 0 = 灭）
  -> devm_led_classdev_register_ext()               注册 led_classdev：
         name = "dp-led"、max_brightness = 1、
         brightness_set_blocking = dp_led_set_brightness()（内部 gpiod_set_value_cansleep 写引脚）
  -> 结果：出现 /sys/class/leds/dp-led

按键侧（interrupt_key.c）：
  compatible 匹配 -> interrupt_key_probe()
  -> devm_kzalloc()     分配私有数据 interrupt_key_data { dev, led_cdev, led_on }
  -> devm_of_led_get(dev, 0)
        按 dts 的 leds = <&dp_led> 在 LED 框架里查已注册的 led_classdev；
        LED 尚未注册时返回 -EPROBE_DEFER，内核稍后会自动重试 probe
  -> platform_get_irq(pdev, 0)   取虚拟中断号(virq)
  -> devm_request_threaded_irq(dev, irq, NULL, key_handle_thread, IRQF_ONESHOT,
                               dev_name(dev), key_data)
        handler 传 NULL + IRQF_ONESHOT：上半部用内核默认函数（只返回 IRQ_WAKE_THREAD），
        真正处理放在中断线程中（内核线程 "irq/<num>-dp-gpio-interrupt"，进程上下文，可以睡眠）

中断发生时的调用链：
  硬件中断 -> 上半部（默认 handler，返回 IRQ_WAKE_THREAD）-> 唤醒 irq 线程
  -> key_handle_thread(irq, dev_id)   dev_id 就是注册时传入的 key_data（内核只保管指针，原样回传）
  -> key_data->led_on 取反 -> led_set_brightness_sync() -> LED 驱动的 dp_led_set_brightness()

四、编译与烧写
1. 编入内核（默认方式），需要三级挂接齐全：
   kernel-6.1/Makefile:      core-y += driver_pratice/     （顶层入口，缺了它整个 driver_pratice 都不会被编译）
   driver_pratice/Makefile:  obj-y  += gpio_interrupt/     （进入本目录）
   本目录 Kbuild:            obj-y  += interrupt-key-led.o / interrupt_key.o
   ./build.sh kernel 编译后，重烧 output/firmware/boot.img
2. 编成模块：把 Kbuild 改名为 Kbuild.bak，执行 make 得到 interrupt-key-led.ko / interrupt_key.ko，
   拷到板上先 insmod interrupt-key-led.ko 再 insmod interrupt_key.ko
   （顺序反了按键驱动会 deferred，重新 insmod 一次按键驱动即可）。
   注意：同一 platform 总线上 driver 的 .name 必须全局唯一，本工程两个驱动分别叫
   chip_gpio / interrupt_key，与 led_bus_device_tree 的 dp_led 不冲突。

五、验证方法
1. dmesg 里应依次出现：
     ----chip_gpio_probe----
     LED registered as /sys/class/leds/dp-led
     ----interrupt_key_probe----
   若只看到 "platform dp-gpio-interrupt: deferred probe pending"，说明 LED 还没注册成功。
2. ls /sys/class/leds/  -> dp-led（可 echo 1/0 > brightness 手动点灯灭灯）
3. ls /sys/bus/platform/drivers/  -> chip_gpio、interrupt_key，且各自目录下有绑定设备的链接
4. cat /proc/interrupts | grep dp-gpio-interrupt   每按一次按键，计数 +1
5. 按按键：dmesg 打印 key_handle_thread，LED 亮灭翻转

六、本实验踩过的坑（排查思路）
1. deferred probe pending：按键驱动在等 LED。先确认 LED 驱动编进内核了
   （/sys/bus/platform/drivers/ 里有没有 chip_gpio），再比对 compatible 字符串
   （"dp_industry,dp_led"，逗号后不能有空格）与 dts 是否完全一致。
2. unable to find group for node xxx：pinctrl 组写法平铺了，必须"功能节点 -> 组节点"两层；
   可用 cat /sys/kernel/debug/pinctrl/*/pinmux-functions 验证：平铺的名字会显示成
   "function xxx, groups = [ ]"（空功能，不是 group）。
3. Driver 'xxx' is already registered：驱动重名，同一总线上 .name 必须唯一。
4. 没有 leds = <&dp_led> 这个 phandle 时，devm_of_led_get 直接返回 -ENOENT，
   按键设备 probe 会真正失败（不会像 EPROBE_DEFER 那样重试）。

七、涉及的内核机制
devm_*：资源随设备生命周期管理，probe 失败/驱动卸载时自动释放，且按注册逆序释放
    （所以 free_irq 一定先于 kfree(key_data)，中断线程不会访问到已释放的内存）
dev_id：注册中断时给内核的 cookie，中断里原样回传；共享中断与 free_irq 也靠它匹配
threaded irq + IRQF_ONESHOT：上半部极短（硬件中断）、下半部线程可睡眠（软件中断）；oneshot 保证线程处理完前中断线保持屏蔽
of_led_get：按 phandle 找 dts 节点 -> 在 led class 中按 of_node 找已注册的 led_classdev

