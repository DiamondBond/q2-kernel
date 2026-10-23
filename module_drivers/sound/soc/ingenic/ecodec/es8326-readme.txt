/*** 板级文件配置 ***/
/*
   static int x1600_i2s_es8326_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
    struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
    struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
    int ret;
    int id = 0;
//    int sysclk = 24576000; // 默认系统时钟频率
    int sysclk = 256 * params_rate(params);
    int div = sysclk / params_rate(params) / 64;
    int aic_mode = 0, codec_mode = 0;
    int prate;
    struct clk *parent = NULL;


#ifdef CONFIG_SND_ASOC_INGENIC_MASTER_MODE
        aic_mode |= SND_SOC_DAIFMT_CBS_CFM;
        codec_mode |= SND_SOC_DAIFMT_CBS_CFM;
#endif
#ifdef CONFIG_SND_ASOC_INGENIC_SLAVE_MODE
        aic_mode |= SND_SOC_DAIFMT_CBM_CFM;
        codec_mode |= SND_SOC_DAIFMT_CBM_CFM;
#endif
#ifdef CONFIG_SND_ASOC_INGENIC_I2S_MODE
        aic_mode |= SND_SOC_DAIFMT_I2S;
        codec_mode |= SND_SOC_DAIFMT_I2S;
#endif
#ifdef CONFIG_SND_ASOC_INGENIC_LEFT_J_MODE
        aic_mode |= SND_SOC_DAIFMT_MSB;
        codec_mode |= SND_SOC_DAIFMT_LEFT_J;
#endif

        parent = clk_get(NULL, "epll");
        prate = clk_get_rate(parent);

    ret = snd_soc_dai_set_sysclk(codec_dai, 0, (256 * params_rate(params)), 0);
    if (ret) {
        printk("Failed to set codec sysclk: %d\n", ret);
        return ret;
    }
     if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
         id |= INGENIC_I2S_PLAYBACK;
     else
         id |= INGENIC_I2S_CAPTURE;

     ret = snd_soc_dai_set_fmt(cpu_dai, aic_mode);
     if (ret)
         return ret;

     ret = snd_soc_dai_set_sysclk(cpu_dai, id, sysclk, SND_SOC_CLOCK_OUT);
     if (ret)
         return ret;

     ret = snd_soc_dai_set_clkdiv(cpu_dai, id, div);
     if (ret)
         return ret;

     return 0;
 }
*/

/***dts 配置 ***/
/*
    &i2c1 {
        status = "okay";
        clock-frequency = <100000>;
        timeout = <1000>;
        pinctrl-names = "default";
        pinctrl-0 = <&i2c1_pb_f0>;
        es8326: codec@18{
                compatible = "everest,es8326";
                status = "okay";
                reg = <0x18>;   //i2c地址
                ingenic,cpvdd-en-gpio = <&gpb 23 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;

         };
 };
&aic {
        status = "okay";
        pinctrl-names = "default";
        pinctrl-0 = <&aic_pb_es8326>;
};

   dump_pcm_codec:dump_pcm_codec{
           compatible = "ingenic,pcm-dump-codec";
           status = "okay";
   };

   sound_halley6_ecdc {
           status = "okay";
           compatible = "ingenic,x1600-sound";
           ingenic,model = "halley6";
           ingenic,dai-link = "i2s-ecodec", "i2s-tloop";
           ngenic,stream = "i2s-ecodec", "i2s-tloop";
           ingenic,cpu-dai = <&i2s>, <&i2s_tloop>;
           ingenic,platform = <&aic>, <&i2s_tloop>;
           ingenic,codec = <&es8326>, <&dump_pcm_codec>;
           ingenic,codec-dai = "ES8326 HiFi", "pcm-dump";
           ingenic,spken-gpios = <&gpc 0 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
   };
*/



