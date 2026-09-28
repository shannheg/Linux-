#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static const struct of_device_id interrupt_key_of_match[] = {
    {.compatible = "dp_industry,dp_key"},
    {}
};


struct interrupt_key_data {
    struct device *dev;
    struct led_classdev *led_cdev;
    bool led_on;
};

//中断的软中断线程，在注册的时候分为硬中断和软中断，软中断线程可以睡眠
//第一个入参为设备分配的虚拟中断号，不是硬中断号，第二个是之前注册时传入的key_data结构体指针，内核只是代为保管，在软中断时取出指针从而获得之前注册时的数据
static irqreturn_t key_handle_thread(int irq, void *dev_id){
    struct interrupt_key_data *key_data = dev_id;
    int ret;

    printk("key_handle_thread\n");
    key_data->led_on = !key_data->led_on;
    //设置led的亮度，第一个入参是led_classdev结构体指针，第二个入参是亮度值，LED_OFF表示关闭，LED_FULL表示全亮
    ret = led_set_brightness_sync(key_data->led_cdev,
                                  key_data->led_on ? LED_FULL : LED_OFF);
    if (ret)
        dev_err_ratelimited(key_data->dev, "Failed to set LED: %d\n", ret);

    return IRQ_HANDLED;
}

static int interrupt_key_probe(struct platform_device *pdev){
    printk("------------------interrupt_key_probe----------------------------\n");
    struct device *dev = &pdev->dev;
    struct interrupt_key_data *key_data;
    int irq;
    int ret;
    //因为中断是一种不可预测的行为，所以led资源需要在堆上进行分配以保证其资源不被丢失，GFP_KERNEL代表可睡眠等待
    //GFP_ATOMIC代表不可睡眠等待，GFP_ATOMIC一般用于中断上下文中，GFP_KERNEL一般用于进程上下文中
    //GFP_NOWAIT代表不可睡眠等待，且不允许内存回收
    //GFP_DMA用于需要DMA可寻址内存的上下文
    key_data = devm_kzalloc(dev, sizeof(*key_data), GFP_KERNEL);
    if (!key_data)
        return -ENOMEM;

    key_data->dev = dev;
    key_data->led_cdev = devm_of_led_get(dev, 0);//传入的dev为按键自己的设备，此处的第一个参数是led的消费者
    if (IS_ERR(key_data->led_cdev))
        return dev_err_probe(dev, PTR_ERR(key_data->led_cdev),
                             "Failed to get dp-led\n");

    irq = platform_get_irq(pdev, 0);
    printk("------------------irq = %d---------------------\n", irq);
    if (irq < 0)
        return irq;
    //返回
    ret = devm_request_threaded_irq(dev, irq, NULL, key_handle_thread,
                                    IRQF_ONESHOT, dev_name(dev), key_data);
    if (ret)
        return dev_err_probe(dev, ret, "Failed to request key IRQ\n");

    platform_set_drvdata(pdev, key_data);
    return 0;
}

static struct platform_driver interrupt_key_driver = {
    .probe = interrupt_key_probe,
    .driver = {
        .name = "interrupt_key",
        .of_match_table = interrupt_key_of_match,
    },
};

module_platform_driver(interrupt_key_driver);
MODULE_DEVICE_TABLE(of, interrupt_key_of_match);
MODULE_LICENSE("GPL");
