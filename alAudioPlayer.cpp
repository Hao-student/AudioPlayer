#include <stdio.h>
#include <iostream>
#include <vector>
#include "al.h"
#include "alc.h"
#define __STDC_CONSTANT_MACROS
#define NUMBUFFERS 4
#define	SERVICE_UPDATE_PERIOD 20
#define MAX_AUDIO_FRME_SIZE 48000*4

extern "C"
{
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
};

using namespace std;

void feedAudioData(ALuint source, ALuint alBufferId, int out_sample_rate, FILE* fpPCM_open, int seek_location)
{
	int ret = 0;
	int ndatasize = 4096;
	char ndata[4096 + 1] = { 0 };
	fseek(fpPCM_open, seek_location, SEEK_SET);
	ret = fread(ndata, 1, ndatasize, fpPCM_open);
	alBufferData(alBufferId, AL_FORMAT_STEREO16, ndata, ndatasize, out_sample_rate);
	alSourceQueueBuffers(source, 1, &alBufferId);
}

int main(int argc, char* argv[])
{
	AVFormatContext* formatContext;//封装格式上下文结构体
	AVStream* audioStream;//代表音频流
	AVCodecContext* codecContext;//编解码器上下文结构体
	AVCodec* codec;//对应一种编解码器

	//用户输入验证
	if (argc != 2)
	{
		printf("Usage：%s 'path of the video/audio file'\n", argv[0]);
	}
	avformat_network_init();
	formatContext = avformat_alloc_context();
	//打开输入文件
	if (avformat_open_input(&formatContext, argv[1], NULL, NULL) != 0)
	{
		printf("Couldn't open the input file\n");
		return -1;
	}
	//获取流信息
	if (avformat_find_stream_info(formatContext, NULL) < 0)
	{
		printf("Couldn't find the stream information\n");
		return -1;
	}
	av_dump_format(formatContext, 0, argv[1], false);
	//查找音频流
	int audioIndex = -1;
	for (int i = 0; i < formatContext->nb_streams; i++)
	{
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioIndex = i;
			break;
		}
	}
	if (audioIndex == -1)
	{
		printf("Couldn't find a audio stream\n");
		return -1;
	}
	//查找解码器
	audioStream = formatContext->streams[audioIndex];
	codecContext = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(codecContext, audioStream->codecpar);
	codec = avcodec_find_decoder(codecContext->codec_id);
	//解决时间戳报错问题
	codecContext->pkt_timebase = formatContext->streams[audioIndex]->time_base;
	if (codec == NULL)
	{
		printf("Couldn't find decoder\n");
		return -1;
	}
	//打开解码器
	if (avcodec_open2(codecContext, codec, NULL) < 0)
	{
		printf("Couldn't open decoder\n");
		return -1;
	}
	//压缩数据
	AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	//解压缩数据
	AVFrame* frame = av_frame_alloc();
	//重采样
	SwrContext* swr = swr_alloc();
	//--------重采样参数设置--------
	//输入采样格式
	enum AVSampleFormat in_sample_fmt = codecContext->sample_fmt;
	//输出采样格式
	enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	//输入采样率
	int in_sample_rate = codecContext->sample_rate;
	//输出采样率
	int out_sample_rate = in_sample_rate;
	//输入声道布局
	uint64_t in_ch_layout = codecContext->channel_layout;
	//输出声道布局为立体声
	uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
	swr_alloc_set_opts(swr,
		out_ch_layout,out_sample_fmt,out_sample_rate,
		in_ch_layout,in_sample_fmt,in_sample_rate,
		0,NULL);
	swr_init(swr);
	//输出声道个数
	int nb_out_channel = av_get_channel_layout_nb_channels(out_ch_layout);
	//--------重采样参数设置--------
	//PCM数据
	uint8_t* out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRME_SIZE);
	int out_buffer_size;
	//打开写入PCM文件
	FILE* fpPCM = fopen("output.pcm", "wb");
	//循环读取压缩数据
	while (av_read_frame(formatContext, packet) >= 0)
	{
		if (packet->stream_index == audioIndex)
		{
			//解码
			int ret = avcodec_send_packet(codecContext, packet);
			if (ret != 0)
			{
				printf("Couldn't submit the packet to the decoder\n");
				exit(1);
			}
			int got_frame = avcodec_receive_frame(codecContext, frame);
			//if (got_frame < 0)
			//{
				//printf("Error during decoding\n");
				//exit(1);
			//}
			if (got_frame == 0)
			{
				swr_convert(swr, &out_buffer, MAX_AUDIO_FRME_SIZE, (const uint8_t**)frame->data, frame->nb_samples);
				//获取sample的大小
				out_buffer_size = av_samples_get_buffer_size(NULL, nb_out_channel,
					frame->nb_samples, out_sample_fmt, 1);
				fwrite(out_buffer, 1, out_buffer_size, fpPCM);
			}
		}
		av_packet_unref(packet);
	}

	//--------初始化openAL--------
	ALCdevice* pDevice = alcOpenDevice(NULL);
	ALCcontext* pContext = alcCreateContext(pDevice, NULL);
	alcMakeContextCurrent(pContext);
	if (alcGetError(pDevice) != ALC_NO_ERROR)
		return AL_FALSE;
	ALuint source;
	alGenSources(1, &source);
	if (alGetError() != AL_NO_ERROR)
	{
		printf("Couldn't generate audio source\n");
		return -1;
	}
	ALfloat SourcePos[] = { 0.0,0.0,0.0 };
	ALfloat SourceVel[] = { 0.0,0.0,0.0 };
	ALfloat ListenerPos[] = { 0.0,0.0 };
	ALfloat ListenerVel[] = { 0.0,0.0,0.0 };
	ALfloat ListenerOri[] = { 0.0,0.0,-1.0,0.0,1.0,0.0 };
	alSourcef(source, AL_PITCH, 1.0);
	alSourcef(source, AL_GAIN, 1.0);
	alSourcefv(source, AL_POSITION, SourcePos);
	alSourcefv(source, AL_VELOCITY, SourceVel);
	alSourcef(source, AL_REFERENCE_DISTANCE, 50.0f);
	alSourcei(source, AL_LOOPING, AL_FALSE);
	//--------初始化openAL--------

	//打开写好的pcm文件
	FILE* fpPCM_open = NULL;
	if ((fpPCM_open = fopen("output.pcm", "rb")) == NULL)
	{
		printf("Failed open the PCM file\n");
		return -1;
	}

	ALuint alBufferArray[NUMBUFFERS];
	alGenBuffers(NUMBUFFERS, alBufferArray);

	//首次填充数据
	int seek_location = 0;
	for (int i = 0; i < NUMBUFFERS; i++)
	{
		feedAudioData(source, alBufferArray[i], out_sample_rate, fpPCM_open,seek_location);
		seek_location += 4096;
	}

	//开始播放
	alSourcePlay(source);

	ALint iTotalBuffersProcessed = 0;
	ALint iBuffersProcessed;
	ALint iState;
	ALuint bufferId;
	ALint iQueuedBuffers;

	while (true)
	{
		iBuffersProcessed = 0;
		alGetSourcei(source, AL_BUFFERS_PROCESSED, &iBuffersProcessed);
		iTotalBuffersProcessed += iBuffersProcessed;
		iTotalBuffersProcessed += iBuffersProcessed;
		while (iBuffersProcessed > 0) {
			bufferId = 0;
			alSourceUnqueueBuffers(source, 1, &bufferId);
			feedAudioData(source, bufferId, out_sample_rate, fpPCM_open,seek_location);
			seek_location += 4096;
			iBuffersProcessed -= 1;
		}
		alGetSourcei(source, AL_SOURCE_STATE, &iState);
		if (iState != AL_PLAYING) {
			alGetSourcei(source, AL_BUFFERS_QUEUED, &iQueuedBuffers);
			if (iQueuedBuffers) {
				alSourcePlay(source);
			}
			else {
				//停止播放
				break;
			}
		}
	}

	alSourceStop(source);
	alSourcei(source, AL_BUFFER, 0);

	alDeleteSources(1, &source);
	alDeleteBuffers(NUMBUFFERS, alBufferArray);

	fclose(fpPCM_open);
	fclose(fpPCM);

	alcMakeContextCurrent(NULL);
	alcDestroyContext(pContext);
	pContext = NULL;
	alcCloseDevice(pDevice);
	pDevice = NULL;

	av_frame_free(&frame);
	av_free(out_buffer);
	swr_free(&swr);
	avcodec_close(codecContext);
	avformat_close_input(&formatContext);

	return 0;
}

