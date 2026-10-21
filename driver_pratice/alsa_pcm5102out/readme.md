# SAI0 + PCM5102A：从设备树解析到 simple-card 声卡注册

本文只依据本目录的 [dp-rk3506g-mini.dts](dp-rk3506g-mini.dts) 和当前 Linux 6.1 源码，说明 SAI0、PCM5102A 如何被驱动识别、如何由 simple-card 连接，以及最终如何注册为 ALSA 声卡。文中的函数名和执行关系均按本源码树整理。

核对日期：2026-10-20。本次仅整理文档、静态核对源码，没有编译、烧录或板上验证。这里的 DTS 是配置样例；实际启动是否使用这些节点，应以最终 DTB 和板上运行设备树为准。

## 1. 先明确三个设备和四个职责

设备树中有三个相关设备节点，ASoC 中则需要组合四个职责：

| 职责 | 设备树来源 | 驱动 | 主要工作 |
| --- | --- | --- | --- |
| CPU DAI | `sai0: sai@ff300000`，本文件用 `&sai0` 覆盖配置 | `sound/soc/rockchip/rockchip_sai.c` | 控制 SAI 寄存器、I2S 格式、BCLK/LRCK、数据收发 |
| PCM Platform | 复用 SAI0 设备，不必单独写一个节点 | `sound/soc/soc-generic-dmaengine-pcm.c` | 管理 PCM 缓冲区和 DMA 传输 |
| Codec DAI | `pcm5102: pcm5102` | `sound/soc/codecs/pcm5102a.c` | 声明 PCM5102A 的播放能力，提供 `pcm5102a-hifi` 接口 |
| Machine / Card | `sai0_sound: sai0-sound` | `sound/soc/generic/simple-card.c` | 解析板级连接关系，将 CPU、Codec、PCM Platform 组成声卡 |

DAI 是 Digital Audio Interface，即数字音频接口。这里 CPU DAI 表示 SoC 的 SAI 接口，Codec DAI 表示外部 DAC 接收音频的接口。`snd_soc_dai_link` 表示两端之间的连接。

`simple-card` 不代替 SAI 驱动，也不代替 PCM5102A 驱动。它保存连接规则，交给 ASoC 核心寻找已经注册的组件，再完成声卡实例化。

## 2. 本设备树提供了哪些信息

### 2.1 PCM5102A 节点

对应本目录 DTS 第 92 行附近：

```dts
pcm5102: pcm5102 {
	compatible = "ti,pcm5102a";
	#sound-dai-cells = <0>;
	status = "okay";
};
```

| 属性 | 含义 | 谁使用 |
| --- | --- | --- |
| `pcm5102:` | DTS 标签，供其他节点通过 `&pcm5102` 引用；编译后通过 phandle 关联 | DTS 编译器和 OF 解析函数 |
| `compatible` | 选择匹配的 Codec 驱动 | platform 总线匹配 `pcm5102a_of_match[]` |
| `#sound-dai-cells = <0>` | 引用此 DAI 时，phandle 后面没有额外参数 | `of_parse_phandle_with_args()` |
| `status = "okay"` | 节点可用 | OF 创建设备及探测流程 |

`#sound-dai-cells = <0>` 中的 `0` 是参数个数，不是声卡号、PCM 设备号或声道数。对于本驱动的单个 DAI，解析时选择索引 0。

这个 Codec 驱动通过 platform 总线注册，没有 I2C/SPI 控制寄存器的操作，也不会读取芯片 ID。软件注册成功不代表已经检测到实体 DAC。

### 2.2 声卡节点

对应本目录 DTS 第 103 行附近：

```dts
sai0_sound: sai0-sound {
	compatible = "simple-audio-card";
	simple-audio-card,name = "rk3506-pcm5102";
	simple-audio-card,format = "i2s";
	simple-audio-card,mclk-fs = <256>;
	simple-audio-card,bitclock-master = <&sai0_cpu>;
	simple-audio-card,frame-master = <&sai0_cpu>;

	sai0_cpu: simple-audio-card,cpu {
		sound-dai = <&sai0>;
	};

	simple-audio-card,codec {
		sound-dai = <&pcm5102>;
		clocks = <&mclkout_sai0>;
	};
};
```

| 属性或子节点 | 解析后的作用 |
| --- | --- |
| `compatible = "simple-audio-card"` | 匹配 `asoc_simple_card` platform driver，进入 `asoc_simple_probe()` |
| `simple-audio-card,name` | 保存为 `snd_soc_card.name`，用于声卡显示名称 |
| `simple-audio-card,format` | 解析为 `dai_link->dai_fmt` 中的 I2S 格式 |
| `simple-audio-card,mclk-fs` | 保存到 `simple_dai_props.mclk_fs`；播放设置参数时按采样率计算 MCLK |
| `bitclock-master` | 指向 CPU 子节点，表示 SAI0 提供 BCLK |
| `frame-master` | 指向 CPU 子节点，表示 SAI0 提供 LRCK |
| CPU 子节点的 `sound-dai` | 找到 SAI0 的设备节点和 CPU DAI |
| Codec 子节点的 `sound-dai` | 找到 PCM5102A 的设备节点和 Codec DAI |
| Codec 子节点的 `clocks` | 由 simple-card 工具函数取得输出到引脚的 MCLK 时钟句柄 |

两个 master 属性引用的是 **声卡内的 CPU 子节点 `&sai0_cpu`**。`sound-dai = <&sai0>` 才引用真正的 SAI 控制器节点；这两类引用用途不同。

CPU、Codec 子节点是 simple-card 的描述数据，由它主动遍历解析；它们不分别创建 CPU/Codec 的 platform device。实际设备来自独立的 `sai0` 和 `pcm5102` 节点。

### 2.3 SAI0 控制器节点与引脚

基础定义在 [rk3502.dtsi](../../arch/arm/boot/dts/rk3502.dtsi) 第 781 行附近，通过本文件的 include 引入。关键内容节选如下：

```dts
sai0: sai@ff300000 {
	compatible = "rockchip,rk3506-sai", "rockchip,sai-v1";
	reg = <0xff300000 0x1000>;
	interrupts = <GIC_SPI 50 IRQ_TYPE_LEVEL_HIGH>;
	clocks = <&cru MCLK_SAI0>, <&cru HCLK_SAI0>;
	clock-names = "mclk", "hclk";
	/* dmas 提供 TX/RX 的 DMA 描述，具体数值见原文件。 */
	dma-names = "tx", "rx";
	resets = <&cru SRST_M_SAI0>, <&cru SRST_H_SAI0>;
	reset-names = "m", "h";
	#sound-dai-cells = <0>;
	/* 省略基础 pinctrl 和其他属性。 */
};
```

本目录 DTS 第 195 行附近覆盖引脚和状态：

```dts
&sai0 {
	pinctrl-names = "default";
	pinctrl-0 = <&rm_io8_sai0_mclk &rm_io9_sai0_sclk
		     &rm_io10_sai0_sdo &rm_io11_sai0_lrck>;
	status = "okay";
};
```

`&sai0 { ... }` 修改已有节点，合并后的同一个节点同时具有寄存器、时钟、DMA 和新的引脚配置。

按 [rk3506-pinctrl-rmio.dtsi](../../arch/arm/boot/dts/rk3506-pinctrl-rmio.dtsi) 中的实际定义：

| pinctrl 标签 | 管脚 | 功能 | 连接到 DAC 的信号 |
| --- | --- | --- | --- |
| `rm_io8_sai0_mclk` | GPIO0_B0 / RM_IO8 | MCLK，复用功能 66 | SCK / MCLK |
| `rm_io9_sai0_sclk` | GPIO0_B1 / RM_IO9 | SCLK，复用功能 67 | BCK / BCLK |
| `rm_io10_sai0_sdo` | GPIO0_B2 / RM_IO10 | SDO，复用功能 73 | DIN |
| `rm_io11_sai0_lrck` | GPIO0_B3 / RM_IO11 | LRCK，复用功能 68 | LCK / LRCK |

此表依据 `pinctrl-0` 引用和 `rockchip,pins` 定义；文件中的旧注释不作为引脚映射依据。这里解释软件配置，实际接线仍需与板子核对。

### 2.4 `mclkout_sai0` 由谁提供

该时钟节点也定义在 `rk3502.dtsi`，第 106 行附近，`compatible = "rockchip,clk-out"`，父时钟为 CRU 的 `MCLK_OUT_SAI0`。

[clk-out.c](../../drivers/clk/rockchip/clk-out.c) 中的 `rockchip_clk_out_probe()` 读取输出名称、门控位和父时钟，用 `clk_hw_register_gate()` 注册门控时钟，再调用 `of_clk_add_hw_provider()` 提供 OF 时钟查询。注册时使用 `CLK_SET_RATE_PARENT`，设置输出时钟频率时可向父时钟传递请求。

因此本配置有两个有关联但用途不同的时钟引用：SAI0 的 `mclk` 用于控制器，声卡 Codec 子节点的 `mclkout_sai0` 用于管理输出到 IO 的 MCLK。后者由 simple-card 解析，并非 `pcm5102a_probe()` 解析。

## 3. 从 DTS 到 platform device，再匹配驱动

这部分是三个驱动共用的设备模型流程。

```text
DTS + 包含的 DTSI
  └─ 构建时预处理、dtc 编译为 DTB
      └─ 启动程序将 DTB 传给内核
          └─ setup_arch() → unflatten_device_tree()
              └─ 建立内存中的 device_node / property 树
                  └─ of_platform_default_populate_init()
                      → of_platform_default_populate()
                      → of_platform_populate()
                      → of_platform_bus_create()
                      → of_platform_device_create_pdata()
                          ├─ SAI0 platform_device
                          ├─ PCM5102A platform_device
                          └─ sai0-sound platform_device
```

| 函数 | 源码位置 | 作用 |
| --- | --- | --- |
| `setup_arch()` | `arch/arm/kernel/setup.c:1095` | ARM 启动初始化，内部调用 `unflatten_device_tree()` |
| `unflatten_device_tree()` | `drivers/of/fdt.c` | 将扁平 DTB 展开为可遍历的设备节点和属性 |
| `of_platform_default_populate_init()` | `drivers/of/platform.c:517` | 通过 `arch_initcall_sync` 安排默认 OF 设备创建 |
| `of_platform_populate()` | `drivers/of/platform.c:465` | 遍历起始节点下的子节点，发起设备创建 |
| `of_platform_bus_create()` | `drivers/of/platform.c:343` | 检查兼容信息，创建设备，并对匹配的总线节点递归 |
| `of_platform_device_create_pdata()` | `drivers/of/platform.c:167` | 跳过不可用或已创建的节点，分配设备并通过 `of_device_add()` 注册 |
| `platform_match()` | `drivers/base/platform.c:1331` | 优先进行 OF 匹配，用节点的 `compatible` 匹配驱动的 `of_match_table` |
| `really_probe()` | `drivers/base/dd.c:584` | 驱动核心执行探测，在进入驱动前处理 pinctrl 等准备工作 |
| `platform_probe()` | `drivers/base/platform.c:1375` | platform 总线的探测入口，调用对应 platform driver 的 `.probe` |

设备的 `dev.of_node` 指向原设备树节点，因此驱动进入 `.probe()` 后可继续读取属性、取得时钟和 DMA 等资源。内核读取的是 DTB 展开后的属性，不是在启动时打开 `.dts` 文本。

三个音频驱动都通过 `module_platform_driver(...)` 注册 platform driver。宏封装驱动初始化和退出：初始化时注册驱动，退出时注销；驱动内建时由内核初始化调用，编成模块时由模块加载触发。设备先出现或驱动先注册都可以完成后续匹配。

SAI0 的 default 引脚由 `really_probe()` 中的 `pinctrl_bind_pins()` 处理。本配置没有 `init` 引脚状态，因此选择 `default`；此过程位于 `rockchip_sai_probe()` 之前。不能把引脚配置归为 simple-card 的工作。

## 4. SAI0 路径：注册 CPU DAI 与 DMA PCM 组件

源码：[rockchip_sai.c](../../sound/soc/rockchip/rockchip_sai.c)。

设备树兼容串中包含 `"rockchip,sai-v1"`，匹配本驱动的 `rockchip_sai_match[]`，然后进入第 1914 行的 `rockchip_sai_probe()`。

### 4.1 `rockchip_sai_probe()` 做什么

按函数中的实际主要顺序：

1. 分配 `struct rk_sai_dev`，保存到设备私有数据，初始化默认状态。
2. 获取 `"h"`、`"m"` 复位控制，映射 `reg` 指定的 MMIO，建立 `regmap`。
3. 获取可选中断并注册 ISR；读取可选时钟校准属性。
4. 通过 `devm_clk_get()` 获取 `"mclk"` 和 `"hclk"`，临时开启 HCLK 以访问寄存器。
5. 读取硬件版本，调用 `rockchip_sai_parse_quirks()`、`rockchip_sai_parse_paths()` 处理可选特性和数据通路。
6. 调用 `rockchip_sai_init_dai()`，构造本实例的 DAI 能力和 DMA 参数。
7. 初始化 runtime PM。
8. 调用 `rockchip_sai_register_platform()`，注册 DMA PCM 组件。
9. 调用 `devm_snd_soc_register_component(..., dai, 1)`，注册 CPU 组件及一个 DAI。
10. 关闭初始化阶段临时开启的 HCLK，返回探测结果。

### 4.2 三个容易混淆的函数

| 函数 | 何时调用 | 作用 |
| --- | --- | --- |
| `rockchip_sai_init_dai()`，第 1190 行 | SAI platform probe 中 | 根据 `dma-names` 判断播放/录音能力，复制 DAI 驱动模板，填写格式、采样率、声道范围和 FIFO DMA 地址；可读取 `rockchip,slot-width` |
| `rockchip_sai_register_platform()`，第 1897 行 | SAI platform probe 中 | 本配置没有特殊 DMA 开关，走 `devm_snd_dmaengine_pcm_register(dev, NULL, 0)` |
| `rockchip_sai_dai_probe()`，第 976 行 | 后续声卡绑定、探测 DAI 时 | 用 `snd_soc_dai_init_dma_data()` 将收发 DMA 参数挂到实际 DAI 实例 |

`rockchip_sai_probe()` 是设备驱动的 probe；`rockchip_sai_dai_probe()` 是 ASoC DAI 的 probe。注册 DAI 描述之后，声卡绑定阶段才调用后者。

DMA PCM 注册继续进入 [soc-generic-dmaengine-pcm.c](../../sound/soc/soc-generic-dmaengine-pcm.c) 的 `snd_dmaengine_pcm_register()`：分配 `dmaengine_pcm`，按 OF 的 `dmas` / `dma-names` 请求通道，初始化一个 ASoC component，再用 `snd_soc_add_component(..., NULL, 0)` 登记。启用动态 DMA 通道选项时，通道释放和后续申请时机会相应变化。

因此 **同一个 SAI0 设备可对应两个 ASoC component**：DMA PCM component 没有 DAI；SAI component 有 CPU DAI。后续都可以通过同一个 SAI0 的 OF 节点参与声卡组装。

## 5. PCM5102A 路径：注册 Codec DAI

源码：[pcm5102a.c](../../sound/soc/codecs/pcm5102a.c)，驱动名 `pcm5102a-codec`，OF 匹配串 `"ti,pcm5102a"`。

```text
pcm5102a_probe()
  └─ devm_snd_soc_register_component(dev,
          &soc_component_dev_pcm5102a, &pcm5102a_dai, 1)
      └─ snd_soc_register_component()
          ├─ snd_soc_component_initialize()
          └─ snd_soc_add_component()
              ├─ snd_soc_register_dais() → snd_soc_register_dai()
              └─ 加入全局 component_list
```

这些公共注册函数在 [soc-devres.c](../../sound/soc/soc-devres.c) 和 [soc-core.c](../../sound/soc/soc-core.c)。`devm_` 包装增加资源托管，在设备解绑时自动执行相应的注销。

`pcm5102a_dai` 的主要声明是：

```c
.name = "pcm5102a-hifi",
.playback = {
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_384000,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
},
```

这声明双声道播放能力，未声明 capture。最终 PCM 能力要结合 CPU、Codec、DMA 和时钟约束确定；不能仅依据 Codec 声明推断任意采样率均能实际播放。

此驱动没有 `.set_fmt`、`.set_sysclk`、`.hw_params` 回调，也不编程 DAC 的模式引脚。后续 ASoC 对不支持的格式或 sysclk 操作会得到 `-ENOTSUPP`，相关调用者允许该返回值；这不影响 SAI 一侧执行实际设置。

这一步只登记组件和 DAI，尚未生成一个完整的 ALSA 声卡。

## 6. simple-card 路径：读取连接并生成 `snd_soc_dai_link`

源码：[simple-card.c](../../sound/soc/generic/simple-card.c) 和 [simple-card-utils.c](../../sound/soc/generic/simple-card-utils.c)。

`simple_of_match[]` 匹配 `"simple-audio-card"`；platform driver 变量名为 `asoc_simple_card`，驱动名为 `"asoc-simple-card"`，入口为 `asoc_simple_probe()`。这些名字分别是匹配串、C 变量和驱动名称，不要求与声卡节点名一致。

### 6.1 `asoc_simple_probe()` 的主线

```text
asoc_simple_probe(pdev)                         simple-card.c:630
  ├─ 分配 asoc_simple_priv，其中内嵌 snd_soc_card
  ├─ 设置 card->dev / owner / probe / driver_name
  ├─ simple_get_dais_count()                    先统计连接数量
  ├─ asoc_simple_init_priv()                    再分配 link / props 等数组
  ├─ simple_parse_of()                         填入设备树的连接与属性
  ├─ snd_soc_card_set_drvdata(card, priv)
  └─ devm_snd_soc_register_card(dev, card)      交给 ASoC 核心绑定
```

`card->probe` 被设为 `simple_soc_probe()`。它是之后声卡级初始化的回调，用于可选的耳机、麦克风插孔设置；它与 platform 入口 `asoc_simple_probe()` 是两个函数。本配置没有这些插孔属性。

### 6.2 为什么遍历了两次设备树

| 阶段 | 函数 | 本配置得到的结果 |
| --- | --- | --- |
| 统计数量 | `simple_get_dais_count()` → `simple_for_each_link()` → `simple_count_noml()` | 1 条 link，1 个 CPU，1 个 Codec，1 个 Platform 描述 |
| 分配内存 | `asoc_simple_init_priv()` | 分配 link、props、2 个 DAI 参数描述、3 个 component 匹配描述，并设置 `card->num_links = 1` |
| 正式解析 | `simple_parse_of()` → `simple_for_each_link()` → `simple_dai_link_of()` | 填入 CPU/Codec/Platform 节点、名称、时钟、格式及回调 |

`simple_for_each_link()` 调用 `__simple_for_each_link()` 遍历节点。本配置没有 `simple-audio-card,dai-link` 容器，因此把声卡顶层作为单条 link，使用 `simple-audio-card,` 前缀查找 CPU、Codec 子节点。

此处使用普通 `simple-audio-card`，走普通 CPU–Codec 连接路径。无需展开 DPCM 分支。

### 6.3 `simple_parse_of()` 解析顺序

1. `asoc_simple_parse_widgets()`：读取可选 DAPM 控件。
2. `asoc_simple_parse_routing()`：读取可选音频路由。
3. `asoc_simple_parse_pin_switches()`：读取可选引脚开关。
4. 遍历连接并调用 `simple_dai_link_of()`，这是当前配置的核心。
5. `asoc_simple_parse_card_name()`：优先尝试 `label`，再读取 `simple-audio-card,name`；此处得到 `rk3506-pcm5102`。
6. `snd_soc_of_parse_aux_devs()`：读取可选辅助设备。

本设备树没有提供上述可选 widgets、routing、pin-switches 和 aux-devs，按缺省路径处理。

### 6.4 `simple_dai_link_of()`：逐端解析，再组装 link

```text
simple_dai_link_of()
  ├─ 找到 CPU、Codec 和可选 plat 子节点
  ├─ simple_parse_node(CPU)
  │   ├─ simple_parse_mclk_fs()
  │   ├─ asoc_simple_parse_dai()
  │   ├─ asoc_simple_parse_clk()
  │   └─ asoc_simple_parse_tdm()
  ├─ simple_parse_node(Codec)                  同样解析上述四类信息
  ├─ asoc_simple_parse_platform()             无 plat 子节点时直接返回
  ├─ 生成 CPU DAI 名称 + Codec DAI 名称组成的 link 名称
  ├─ asoc_simple_canonicalize_cpu()
  ├─ asoc_simple_canonicalize_platform()
  └─ simple_link_init()
      ├─ asoc_simple_parse_daifmt()
      ├─ dai_link->init = asoc_simple_dai_init
      ├─ dai_link->ops = &simple_ops
      └─ asoc_simple_set_dailink_name()
```

各函数的输入和保存结果：

| 函数 | 输入 / 读取内容 | 保存结果和作用 |
| --- | --- | --- |
| `simple_parse_mclk_fs()` | 声卡顶层及当前节点相关的 `mclk-fs` | 本配置保存 `props->mclk_fs = 256`，本函数不按采样率设置时钟 |
| `asoc_simple_parse_dai()` | CPU 或 Codec 子节点的 `sound-dai` | 保存 `snd_soc_dai_link_component.of_node` 和 `.dai_name`，CPU 侧还返回是否无额外 phandle 参数 |
| `asoc_simple_parse_clk()` | 子节点的 `clocks` / `system-clock-frequency` 等，缺省时尝试 DAI 提供者的时钟 | 保存 `asoc_simple_dai` 中的时钟句柄、当前 sysclk、方向及固定时钟标记 |
| `asoc_simple_parse_tdm()` | `dai-tdm-slot-*` | 这是调用 `snd_soc_of_parse_tdm_slot()` 的宏；本配置未指定，保留缺省值 |
| `asoc_simple_canonicalize_cpu()` | `#sound-dai-cells = <0>` 对应的单 DAI 情况 | 清空 CPU 匹配描述的 `.dai_name`，后续依靠 OF 节点匹配，避免名称不一致 |
| `asoc_simple_canonicalize_platform()` | 未设置的 Platform 节点、CPU 匹配描述 | 将 Platform `.of_node` 缺省设为 SAI0 节点，以找到 SAI0 设备上的 DMA PCM 组件 |
| `simple_link_init()` | 声卡/link 节点、Codec 子节点 | 设置格式、初始化回调、运行期操作及 link 名称 |

对于本配置，CPU 子节点没有 `clocks`，`asoc_simple_parse_clk()` 会尝试从 SAI0 提供者取得时钟频率作为 `sysclk`；这个回退分支不把句柄存入 `simple_dai->clk`。Codec 子节点显式写了 `clocks = <&mclkout_sai0>`，成功取得后则同时保存句柄和频率，供 startup / hw_params 等阶段使用。

### 6.5 `sound-dai = <&pcm5102>` 怎样找到 `pcm5102a-hifi`

```text
asoc_simple_parse_dai(codec_child, codecs, NULL)
  ├─ of_parse_phandle_with_args(codec_child,
  │       "sound-dai", "#sound-dai-cells", 0, &args)
  │   └─ args.np = PCM5102A 节点，args.args_count = 0
  └─ snd_soc_of_get_dai_name(codec_child, &codecs->dai_name)
      ├─ 再解析 sound-dai 的 phandle
      └─ snd_soc_get_dai_name(&args, ...)
          ├─ 在全局 component_list 中寻找 OF 节点匹配的组件
          ├─ 跳过没有 DAI 的组件
          ├─ 本驱动未提供专用转换回调，按缺省规则取第 0 个 DAI
          └─ 得到 "pcm5102a-hifi"
```

`of_parse_phandle_with_args()` 只负责引用和参数的解析；**将设备节点对应到已注册 ASoC 组件，并取得 DAI 名称，是 `snd_soc_get_dai_name()` 的工作**。因此 PCM5102A 驱动没注册时，设备树节点即使存在，也不能完成这个步骤。

CPU 一侧过程相同，找到 SAI0 上具有 DAI 的组件。DMA PCM component 没有 DAI，不会被误当作 CPU DAI。

### 6.6 I2S 与 master 属性怎样变为 SAI 寄存器设置

`asoc_simple_parse_daifmt()` 先调用 `snd_soc_daifmt_parse_format()` 解析 `"i2s"`，再调用 `snd_soc_daifmt_parse_clock_provider_as_phandle()` 取得两个 master 引用。

函数比较 master 引用是否等于 Codec 子节点。本配置都指向 CPU 子节点，所以保存的 `dai_link->dai_fmt` 表示 **Codec 接收 BCLK 和 LRCK**。没有反相属性时使用正常时钟极性。

之后在声卡绑定阶段：

```text
soc_init_pcm_runtime()
  └─ snd_soc_runtime_set_dai_fmt(rtd, dai_link->dai_fmt)
      ├─ Codec 端：snd_soc_dai_set_fmt(codec_dai, dai_fmt)
      │   └─ PCM5102A 无 set_fmt；-ENOTSUPP 被允许
      ├─ snd_soc_daifmt_clock_provider_flipped(dai_fmt)
      │   └─ 将时钟提供/接收关系转换为 CPU 端视角
      └─ CPU 端：snd_soc_dai_set_fmt(cpu_dai, 转换后的格式)
          └─ rockchip_sai_set_fmt()
              ├─ SAI_CKR：主模式、时钟极性
              └─ rockchip_sai_fmt_create()：I2S 对齐和帧格式
```

`rockchip_sai_set_fmt()` 收到的主模式标志为 `SND_SOC_DAIFMT_BP_FP`，设置 `sai->is_master_mode = true`。不要把 link 中以 Codec 为视角的 master 标志直接当作 SAI 收到的标志；中间存在翻转。

### 6.7 解析完成后，内存里的连接是什么样

结构定义见 [simple_card_utils.h](../../include/sound/simple_card_utils.h)。以下为逻辑示意，不是可编译的初始化代码：

```text
asoc_simple_priv
  ├─ snd_card                         类型是 struct snd_soc_card
  │   ├─ name = "rk3506-pcm5102"
  │   ├─ driver_name = "simple-card"
  │   ├─ num_links = 1
  │   └─ dai_link → dai_link[0]
  ├─ dai_link[0]
  │   ├─ cpus[0]     → of_node = sai0，dai_name = NULL
  │   ├─ codecs[0]   → of_node = pcm5102，dai_name = "pcm5102a-hifi"
  │   ├─ platforms[0]→ of_node = sai0
  │   ├─ dai_fmt     = I2S + Codec 接收 BCLK/LRCK + 正常极性
  │   ├─ init        = asoc_simple_dai_init
  │   └─ ops         = simple_ops
  └─ dai_props[0]
      ├─ mclk_fs = 256
      ├─ cpu_dai   → sysclk 等参数
      └─ codec_dai → mclkout_sai0 时钟句柄、sysclk 等参数
```

这里的 `asoc_simple_dai` 是 simple-card 保存的参数；真正已注册的运行期 DAI 实例是 `struct snd_soc_dai`。`snd_soc_dai_link_component` 则是用于查找组件的匹配描述。

## 7. 从 `devm_snd_soc_register_card()` 到 ALSA 设备

### 7.1 注册入口与实际绑定

```text
asoc_simple_probe()
  └─ devm_snd_soc_register_card(dev, card)      soc-devres.c:102
      └─ snd_soc_register_card(card)           soc-core.c:2298
          ├─ 检查 name / dev，初始化链表和锁
          └─ snd_soc_bind_card(card)          soc-core.c:1934
```

`devm_snd_soc_register_card()` 是带自动清理的包装；真正组合组件、创建 PCM 并发布声卡的是后面的 ASoC 和 ALSA 核心。

### 7.2 `snd_soc_bind_card()` 的主要顺序

1. 初始化声卡 DAPM，处理辅助设备和预定义 link。
2. 对每条 link 调用 `snd_soc_add_pcm_runtime()`，建立 `snd_soc_pcm_runtime`（下文简称 rtd）。
3. 调用 `snd_card_new()`，分配底层 ALSA `struct snd_card`。
4. 执行声卡级 probe，本配置最终调用 `simple_soc_probe()`。
5. `soc_probe_link_components()` 探测参与该声卡的组件。
6. `soc_probe_link_dais()` 探测 DAI，其中调用 SAI 的 `rockchip_sai_dai_probe()`。
7. 对每个 rtd 调用 `soc_init_pcm_runtime()`，初始化 link、设置格式并创建 PCM。
8. 完成 DAPM、控件、路由和声卡名称等设置。
9. 调用 `snd_card_register(card->snd_card)`，将 ALSA 设备注册出去。
10. 设置 `card->instantiated = 1`，同步 DAPM 状态。

`struct snd_soc_card` 保存 ASoC 的组件与连接；它的 `snd_card` 成员指向底层 ALSA `struct snd_card`。两者承担不同层次的工作。

### 7.3 `snd_soc_add_pcm_runtime()` 怎样真正找到两端

该函数在 `soc-core.c:965`：

- 分配 rtd，把声明式的 `snd_soc_dai_link` 转成实际运行期连接。
- 对 CPU 匹配描述调用 `snd_soc_find_dai()`：以 `sai0` OF 节点找到 CPU DAI。
- 对 Codec 匹配描述调用 `snd_soc_find_dai()`：以 `pcm5102` OF 节点和 `pcm5102a-hifi` 找到 Codec DAI。
- 按 Platform 匹配描述遍历 component，把同属 SAI0 节点的匹配组件加入 rtd，其中包括 DMA PCM component。

`snd_soc_find_dai()` 在全局组件列表中先匹配 component，再在其 DAI 列表中查找对应 DAI。查不到 CPU 或 Codec DAI 时，这一阶段返回 `-EPROBE_DEFER`。

因此 `sound-dai` 有两次关键用途：解析阶段取得节点和 DAI 名称；绑定阶段再根据这些描述找到实际 `snd_soc_dai` 实例。

### 7.4 `soc_init_pcm_runtime()` 与 `asoc_simple_dai_init()`

`soc_init_pcm_runtime()` 位于 `soc-core.c:1244`，主要流程为：

```text
soc_init_pcm_runtime()
  ├─ snd_soc_link_init()
  │   └─ dai_link->init → asoc_simple_dai_init()
  │       └─ asoc_simple_init_dai()，分别处理 Codec、CPU
  │           ├─ sysclk 非零时调用 snd_soc_dai_set_sysclk()
  │           └─ slots 非零时调用 snd_soc_dai_set_tdm_slot()
  ├─ snd_soc_runtime_get_dai_fmt()
  ├─ snd_soc_runtime_set_dai_fmt()             应用第 6.6 节的 I2S/主从设置
  └─ soc_new_pcm(rtd, num)                    创建普通 PCM
```

这里可按解析时取得的 `sysclk` 设置一次初始时钟；`mclk-fs = 256` 对应的“采样率 × 256”计算则在后续播放的 `hw_params` 中执行。不能把所有时钟设置都归到同一阶段。

`soc_new_pcm()` 在 [soc-pcm.c](../../sound/soc/soc-pcm.c) 第 2868 行附近：根据两端支持的方向确定 playback/capture，调用 `soc_create_pcm()`，内部用 `snd_pcm_new()` 创建 PCM，并安装 `soc_pcm_open`、`soc_pcm_hw_params`、`soc_pcm_trigger` 等操作。

本 Codec 只有双声道 playback，因此该普通 link 提供播放 PCM；SAI0 自身支持 RX 不会令这个 Codec 连接自动具备录音能力。

### 7.5 最终什么时候出现 `/dev/snd` 设备

[sound/core/init.c](../../sound/core/init.c) 的 `snd_card_register()` 调用 `snd_device_register_all()` 注册声卡下的设备，其中 PCM 的设备回调是 [sound/core/pcm.c](../../sound/core/pcm.c) 的 `snd_pcm_dev_register()`，再通过 `snd_register_device()` 注册对应字符设备。

若该声卡分配为 card 0，第一条播放 PCM 分配为 device 0，并且系统设备节点管理正常，可看到：

```text
/proc/asound/cards
/proc/asound/pcm
/dev/snd/controlC0
/dev/snd/pcmC0D0p
```

声卡编号取决于系统实际注册情况，不由 `sai0` 中的数字或 `#sound-dai-cells` 决定。`snd_pcm_new()` 创建内部 PCM 对象，`snd_card_register()` 负责后续发布，二者不能视为同一步。

## 8. 驱动探测先后顺序与延迟探测

三个 platform driver 独立匹配设备，不能把文档章节顺序当成内核保证的 probe 顺序。

对于当前设备树路径，常见情况是：

```text
simple-card 先进入 asoc_simple_probe()
  └─ simple_parse_of()
      └─ asoc_simple_parse_dai()
          └─ snd_soc_get_dai_name()
              └─ CPU 或 Codec component 尚未登记 → -EPROBE_DEFER

依赖驱动就绪，驱动核心重试 simple-card probe
  └─ 重新解析设备树 → 找到两端 DAI → 绑定并注册声卡
```

`-EPROBE_DEFER` 表示依赖暂未就绪。若驱动根本未启用，或相应设备一直没有成功 probe，声卡就不能仅靠等待完成注册。

另一个相关机制是 `snd_soc_add_component()` 成功后调用 `snd_soc_try_rebind_card()`，处理 ASoC 自己维护的 `unbind_card_list`。它主要用于已解绑声卡的重新绑定；不要将本配置首次解析失败后的驱动核心重试，全部解释成这个函数的作用。

## 9. 注册完成之后：`mclk-fs` 和播放回调何时生效

以下是注册流程的后续，用来说明设备树参数最终如何作用于硬件。

`simple_link_init()` 安装的 `simple_ops` 在 `simple-card.c` 文件开头：

```c
static const struct snd_soc_ops simple_ops = {
	.startup  = asoc_simple_startup,
	.shutdown = asoc_simple_shutdown,
	.hw_params = asoc_simple_hw_params,
};
```

| 阶段 | 关键函数 | 本配置中的作用 |
| --- | --- | --- |
| 打开 PCM | `soc_pcm_open()` → link startup → `asoc_simple_startup()` | 开启保存的 CPU/Codec 时钟句柄，处理固定时钟约束；本配置显式保存了 Codec 侧 MCLK 输出句柄 |
| 配置播放参数 | `soc_pcm_hw_params()` → `__soc_pcm_hw_params()` → link hw_params → `asoc_simple_hw_params()` | 计算 `MCLK = params_rate(params) × 256`，设置保存的时钟频率，并调用组件和两端 DAI 的 sysclk 接口 |
| 配置 SAI | 同一 `__soc_pcm_hw_params()` 随后调用 CPU DAI hw_params → `rockchip_sai_hw_params()` | 设置有效位宽、通道/数据线、帧长和 BCLK 分频 |
| 准备播放 | `rockchip_sai_prepare()` 等 | 准备控制器和时钟输出 |
| 启动播放 | `soc_pcm_trigger()` → component / DAI trigger → `rockchip_sai_trigger()` 等 | 启动 DMA 和 SAI 传输 |
| 关闭 PCM | link shutdown → `asoc_simple_shutdown()` | 释放运行期时钟使用；SAI 和 DMA 也执行各自清理 |

`asoc_simple_hw_params()` 对 CPU 端调用 `snd_soc_dai_set_sysclk(..., SND_SOC_CLOCK_OUT)`，最终进入 `rockchip_sai_set_sysclk()`，设置 SAI 的 `mclk`。Codec 端没有对应回调，允许 `-ENOTSUPP`。

例如实际协商为 48 kHz，则此配置请求：

```text
MCLK = 48000 × 256 = 12.288 MHz
LRCK = 48000 Hz
BCLK = 采样率 × 每条数据线的时隙数 × 时隙宽度 × fw_ratio
```

普通双声道 I2S、单数据线、`fw_ratio = 1`，若时隙为 32 bit，则 BCLK 为 `48000 × 2 × 32 = 3.072 MHz`。有效数据为 S16 不代表时隙一定是 16 bit；`rockchip_sai_hw_params()` 从硬件配置取得 slot width，按它计算 BCLK。

## 10. 按源码定位问题与当前静态核对结果

| 现象或需要确认的事项 | 优先查看的位置 |
| --- | --- |
| SAI0 没有绑定驱动 | 最终运行设备树的 `status` / `compatible`、`rockchip_sai_probe()` 返回值、pinctrl/clock/reset/DMA 依赖 |
| PCM5102A 节点存在但无 Codec DAI | `CONFIG_SND_SOC_PCM5102A`、`pcm5102a_probe()`、组件注册结果 |
| simple-card 报 `parse error` 或延迟探测 | `simple_parse_of()` → `asoc_simple_parse_dai()` → `snd_soc_get_dai_name()`；检查两端组件是否就绪 |
| 绑定阶段报 CPU/CODEC DAI 未注册 | `snd_soc_add_pcm_runtime()`、`snd_soc_find_dai()`、link 的 OF 节点与 DAI 名称 |
| 有声卡但播放参数配置失败 | `asoc_simple_hw_params()`、`rockchip_sai_set_sysclk()`、`rockchip_sai_hw_params()`、时钟可实现频率和格式约束 |
| 声卡/PCM 已注册但没有正确模拟输出 | 注册只证明软件组合成功，仍需核对实际 I2S 信号、接线和 DAC 工作状态 |

2026-10-20 静态读取本内核目录 `.config` 的结果如下：

```text
CONFIG_SND_SOC_GENERIC_DMAENGINE_PCM=y
CONFIG_SND_SOC_ROCKCHIP_SAI=y
CONFIG_SND_SIMPLE_CARD_UTILS=y
CONFIG_SND_SIMPLE_CARD=y
# CONFIG_SND_SOC_PCM5102A is not set
```

因此当前这份 `.config` 尚不满足 PCM5102A Codec 驱动参与上述流程的条件。此处只记录现状，没有修改配置；这也不能直接说明板上运行内核采用了相同配置。

如需在板上核对注册结果，可执行以下只读命令，声卡编号按实际情况替换：

```sh
cat /proc/asound/cards
cat /proc/asound/pcm
ls -l /dev/snd
ls -l /sys/bus/platform/drivers/rockchip-sai
ls -l /sys/bus/platform/drivers/pcm5102a-codec
ls -l /sys/bus/platform/drivers/asoc-simple-card
dmesg | grep -Ei 'sai|asoc|pcm5102|simple.card|defer|clock|dma|pinctrl'
```

## 11. 建议沿这条调用链阅读源码

```text
设备树节点 → platform 总线匹配
  │
  ├─ rockchip_sai_probe()
  │   ├─ rockchip_sai_init_dai()
  │   ├─ rockchip_sai_register_platform() → DMA PCM component
  │   └─ devm_snd_soc_register_component() → CPU DAI
  │
  ├─ pcm5102a_probe()
  │   └─ devm_snd_soc_register_component() → pcm5102a-hifi
  │
  └─ asoc_simple_probe()
      ├─ simple_get_dais_count()
      ├─ asoc_simple_init_priv()
      ├─ simple_parse_of()
      │   └─ simple_dai_link_of()
      │       ├─ simple_parse_node() × 2
      │       │   └─ sound-dai → OF 节点 → 已注册组件的 DAI 名称
      │       ├─ canonicalize CPU / Platform
      │       └─ simple_link_init() → 格式、init、ops
      └─ devm_snd_soc_register_card()
          └─ snd_soc_register_card()
              └─ snd_soc_bind_card()
                  ├─ snd_soc_add_pcm_runtime() → 找到实际两端 DAI 和组件
                  ├─ snd_card_new()
                  ├─ 组件/DAI probe → rockchip_sai_dai_probe()
                  ├─ soc_init_pcm_runtime()
                  │   ├─ asoc_simple_dai_init()
                  │   ├─ 设置 dai_fmt → rockchip_sai_set_fmt()
                  │   └─ soc_new_pcm() → snd_pcm_new()
                  └─ snd_card_register() → ALSA 设备可见
```

图中上面的三条分支表达依赖关系，不表示固定探测顺序。到 `snd_card_register()` 成功，才完成这里讨论的声卡注册；按采样率配置时钟和启动 DMA 则发生在后续播放阶段。
