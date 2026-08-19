

static int chip_gpio_probe(struct platform_device *dev)
{
    //记录board_demo中的led引脚，创建设备节点
    int i = 0;
    struct resource *res;
    
    while(1){
        res = platform_get_resource(dev, IORESOURCE_MEM1, i++);
        if(res == NULL){
            break;
        }
        g_ledpins[g_ledcnt++] = res->start;
        led_device_create(g_ledcnt);
        g_ledcnt++;

    }
    printk("%s %s line %d\n", __FILE__, __func__, __LINE__);
    return 0;
}

static int chip_gpio_remove(struct platform_device *dev)
{
    //移除相关引脚
    for(int i = 0; i < g_ledcnt; i++){
        led_device_destroy(i);
    }
    g_ledcnt = 0;
    printk("%s %s line %d\n", __FILE__, __func__, __LINE__);
    return 0;
}

static struct platform_driver chip_gpio = {
    .probe = chip_gpio_probe,
    .remove = chip_gpio_remove,
    .driver = {
        .name = "dp_led",
        .owner = THIS_MODULE,
    },

};

static int chip_init(void)
{
    int ret;
    ret = platform_driver_register(&chip_gpio);
    return 0;
}

static void chip_exit(void)
{
    platform_driver_unregister(&chip_gpio);
    return 0;
}

module_init(chip_init);
module_exit(chip_exit);
module_license("GPL");