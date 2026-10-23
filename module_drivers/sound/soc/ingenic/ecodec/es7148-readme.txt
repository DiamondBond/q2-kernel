板级文件配置：
static int x2660_i2s_ecdc_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd,0);
        struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
        int ret = 0;
        int id = 0;
        int sysclk = 24576000;
        int div = 24576000 / params_rate(params) / 64;
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

         ret = snd_soc_dai_set_fmt(cpu_dai, aic_mode);
         if (ret)
                 return ret;

         ret = snd_soc_dai_set_fmt(codec_dai, codec_mode);
         if (ret)
                 return ret;

         ret = snd_soc_dai_set_sysclk(cpu_dai, id, sysclk, SND_SOC_CLOCK_OUT);
         if (ret)
                 return ret;

         if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
                 id |= INGENIC_I2S_PLAYBACK;
         } else {
                 id |= INGENIC_I2S_CAPTURE;
         }

         ret = snd_soc_dai_set_clkdiv(cpu_dai, id, div);
         if (ret){
                 return ret;
         }
         return 0;
 }


dts 配置:

&aic {
        status = "okay";
        pinctrl-names = "default";
        pinctrl-0 = <&aic_pc>;
};

es7148: es7148{
         compatible = "Everest-semi,es7148";
         status = "okay";
};

sound_x2600e_cdc {
         status = "okay";
         compatible = "ingenic,x2660-halley-sound";
         ingenic,model = "x2660";
         ingenic,dai-link = "i2s-ecodec", "i2s-icodec", "dmic", "i2s-tloop";
         ingenic,stream = "i2s-ecodec", "i2s-icodec", "dmic", "i2s-tloop";
         ingenic,cpu-dai = <&i2s>, <&i2s>, <&dmic>, <&i2s_tloop>;
         ingenic,platform = <&aic>, <&aic>, <&dmic>, <&i2s_tloop>;
         ingenic,codec =  <&es7148>, <&icodec>, <&dmic>, <&dump_pcm_codec>;
         ingenic,codec-dai = "es7148-hifi", "icodec", "dmic-dump-codec", "pcm-dump";
         ingenic,audio-routing = "Speaker", "HPOUTL", "DACL", "MICBIAS" ,
                 "ADCL",  "MICL",
                 "ADCR",  "MICR";
};

NOTE : 如果硬件连接x2600底板的pc15 pc16 ，要拆掉0Ω 电阻R191,否则bclk 和mclk 会互相影响；
