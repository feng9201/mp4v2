#include <stdio.h>
#include <string.h>
#include <stdlib.h>
//#include <unistd.h>
#include <tuple>
#include <utility>
#include <string>
#include "mp4v2/mp4v2.h"
#include <iostream>
#include <chrono>
#include <iomanip> // 用于 std::put_time
#include <thread>

// 编译时Makefile里控制
//#ifdef ENABLE_//DEBUG
//	#define //DEBUG(fmt, args...) 	printf(fmt, ##args)
//#else
//	#define //DEBUG(fmt, args...)
//#endif


int unpackMp4File();


unsigned char g_sps[64] = { 0 };
unsigned char g_pps[64] = { 0 };
unsigned int  g_spslen = 0;
unsigned int  g_ppslen = 0;


int main()
{
	/*if(argc < 2)
	{
		printf("Usage: \n"
			   "   %s ./avfile/test1.mp4 ./test1_out.h264 ./test1_out.aac\n"
			   "   %s ./avfile/test2.mp4 ./test2_out.h264 ./test2_out.aac\n",
				argv[0], argv[0]);
		return -1;
	}*/
	//std::string("E:/avfile/test1.mp4"); , std::string("E:/avfile/test1.h264").c_str(), std::string("E:/avfile/test1.aac").c_str()
	int ret = unpackMp4File();
	if (ret == 0)
	{
		printf("\033[32mSuccess!\033[0m\n");
	}
	else
	{
		printf("\033[31mFailed!\033[0m\n");
	}

	return 0;
}

namespace demuxUtils {

	union endian {
		uint32_t i;
		char c[4];
	};

	static endian e = { 1 };
	static bool isBigEndian = e.c[0] == 0;

	template <class T>
	static T valueFromBtye(char* in, const size_t length_ = -1) noexcept(false) {

		size_t length = (length_ == -1 ? sizeof(T) : length_);

		char* bytes = in;
		T sum = 0;
		for (int i = 0; i < length; ++i) {
			if (isBigEndian)
				sum += ((uint8_t) * (bytes + i) << (8 * i));
			else
				sum += ((uint8_t) * (bytes + i) << (8 * (length - i - 1)));
		}
		return sum;
	}

	template<class T>
	static void valueToByte(T value, char* in, const size_t length_ = -1) noexcept(false) {

		size_t length = (length_ == -1 ? sizeof(T) : length_);

		char* bytes = in;
		for (int i = 0; i < length; ++i) {
			if (isBigEndian)
				*(bytes + i) = (value >> (8 * i)) & 0xff;
			else
				*(bytes + i) = (value >> (8 * (length - i - 1))) & 0xff;
		}
	}
}// namespace demuxUtils

uint32_t ParserData(uint8_t* data, const uint32_t& size_) {
	char* p = (char*)data;
	char* last_p = p;
	uint32_t size = 0;
	uint32_t value = 0x00000001;
	while (p - (char*)data + 4 < size_) {
		size = demuxUtils::valueFromBtye<uint32_t>(p);
		demuxUtils::valueToByte<uint32_t>(value, p);
		
		//char lastFourBits = p[4] & 0x0F;
		//if (lastFourBits == 0x05) {
		//	p[4] = 0x65;
		//}
		//else if (lastFourBits == 0x07) {
		//	p[4] = 0x67;
		//}
		//else if (lastFourBits == 0x08) {
		//	p[4] = 0x68;
		//}
		//else if (lastFourBits == 0x06) {  //sei
		//	p[4] = 0x06;
		//}
		//else if (lastFourBits == 0x01) {
		//	if (p[4] == 0x61) {   //P
		//		int i = 0;
		//	}
		//	else if (p[4] == 0x21) {   //P帧
		//		p[4] = 0x41;
		//	}
		//	else if (p[4] == 0x41) {  //  P帧
		//		int i = 0;
		//	}
		//	else if (p[4] == 0x01) {  //  B帧
		//		int i = 0;
		//	}
		//	else {
		//		int i = 0;
		//	}
		//}

		last_p = p;
		p += (4 + size);
	}
	return last_p - (char*)data;
}

bool isSame(const unsigned char* sps,uint8_t* sDst,int len)
{
	bool are_same = true;
	for (int i = 0; i < len; ++i) {
		if (sps[i] != sDst[i]) {
			are_same = false;
			break;
		}
	}
	return are_same;
}

int getH264Stream(MP4FileHandle mp4Handler, int videoTrackId, int totalSamples, const char* saveFileName )
{
	// 调用的接口要传的参数
	uint32_t curFrameIndex = 1; // `MP4ReadSample`函数的参数要求是从1开始，但我们打印帧下标还是从0开始
	uint8_t* pData = NULL;
	uint32_t nSize = 0;
	MP4Timestamp pStartTime;
	MP4Duration pDuration;
	MP4Duration pRenderingOffset;
	bool pIsSyncSample = 0;

	// 写文件要用的参数
	char naluHeader[4] = { 0x00, 0x00, 0x00, 0x01 };
	char keyHeader[6] = { 0x00, 0x00, 0x00, 0x02,0x09,0x10 };
	char frameHeader[6] = { 0x00, 0x00, 0x00, 0x02,0x09,0x30 };
	FILE* fpVideo = NULL;

	if (!mp4Handler)
		return -1;

	fpVideo = fopen(saveFileName, "wb");
	if (fpVideo == NULL)
	{
		printf("open file(%s) error!\n", saveFileName);
		return -1;
	}

	uint32_t		timescale = MP4GetTrackTimeScale(mp4Handler, videoTrackId);
	uint64_t		msectime = 0;

	int key_count = 0;
	using clock = std::chrono::high_resolution_clock;
	using milliseconds = std::chrono::milliseconds;
	auto startde = clock::now();
	while (curFrameIndex <= totalSamples)
	{
		// 如果传入MP4ReadSample的视频pData是null，它内部就会new 一个内存
		// 如果传入的是已知的内存区域，则需要保证空间bigger then max frames size.
		MP4ReadSample(mp4Handler, videoTrackId, curFrameIndex, &pData, &nSize, &pStartTime, &pDuration, &pRenderingOffset, &pIsSyncSample);

		//计算发送时间 => PTS => 刻度时间转换成毫秒..
		msectime = MP4GetSampleTime(mp4Handler, videoTrackId, curFrameIndex);
		msectime *= UINT64_C(1000);
		msectime /= timescale;

		// 计算开始时间 => DTS => 刻度时间转换成毫秒...
		pStartTime *= UINT64_C(1000);
		pStartTime /= timescale;

		// 当前帧展示时间
		auto dur = pDuration * 1000 / timescale;
		//continue;

		////本地时间
		//auto endde = clock::now();
		//auto now = std::chrono::system_clock::now();
		//// 转换为 time_t，适用于 std::ctime 函数族
		//std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
		//// 使用 localtime 获取本地时间
		//std::tm* local_tm = std::localtime(&now_time_t);
		//// 使用 std::put_time 将 tm 结构体转换为字符串形式
		//std::cout << "Current time: "
		//	<< std::put_time(local_tm, "%Y-%m-%d %H:%M:%S") <<"; play startTime: " << msectime << std::endl;

		//auto durationde = std::chrono::duration_cast<milliseconds>(endde - startde);
		//int64_t st = msectime - durationde.count();
		//st = st > 0 ? st : 1;
		//std::this_thread::sleep_for(std::chrono::milliseconds(st));

		if (pIsSyncSample)
		{
			key_count++;
			//DEBUG("IDR\t");

			/*fwrite(naluHeader, 4, 1, fpVideo);
			fwrite(g_sps, g_spslen, 1, fpVideo);

			fwrite(naluHeader, 4, 1, fpVideo);
			fwrite(g_pps, g_ppslen, 1, fpVideo);*/
			int  i = 0;
		}
		else
		{
			//DEBUG("SLICE\t");
		}

		if (pData && nSize > 4)
		{
			uint32_t len = ParserData(pData, nSize);
			if (pIsSyncSample) {
				if (!(isSame(g_sps, pData + 4, g_spslen) || isSame(g_sps, pData + 10, g_spslen))) {
					fwrite(naluHeader, 4, 1, fpVideo);
					fwrite(g_sps, g_spslen, 1, fpVideo);

					fwrite(naluHeader, 4, 1, fpVideo);
					fwrite(g_pps, g_ppslen, 1, fpVideo);
				}
			}

			fwrite(pData, nSize, 1, fpVideo);

			// `MP4ReadSample`函数的参数要求是从1开始，但我们打印帧下标还是从0开始；而大小已经包含了4字节的start code长度
			//DEBUG("frame index: %d\t size: %d\n", curFrameIndex - 1, nSize);
			//fwrite(naluHeader, 4, 1, fpVideo);
			//fwrite(pData + 4, nSize - 4, 1, fpVideo); // pData+4了，那nSize就要-4
		}
		//pData = pData - 6;
		if (pData) {
			free(pData);
			pData = NULL;
		}

		curFrameIndex++;
	}

	std::cout << "total: " << totalSamples <<"key_sample:" << key_count<< std::endl;

	fflush(fpVideo);
	fclose(fpVideo);

	return 0;
}


#define ADTS_HEADER_LEN  7; // ADTS头部长度定义为7字节

// 采样频率数组，用于根据采样频率索引值查找具体的采样频率
const int sampling_frequencies[] = {
	96000,  // 0x0
	88200,  // 0x1
	64000,  // 0x2
	48000,  // 0x3
	44100,  // 0x4
	32000,  // 0x5
	24000,  // 0x6
	22050,  // 0x7
	16000,  // 0x8
	12000,  // 0x9
	11025,  // 0xa
	8000    // 0xb
	// 0xc d e f是保留的
};

// 构建ADTS头部的函数
int adts_header(uint8_t* const p_adts_header, const int data_length,
	const int profile, const int samplerate,
	const int channels) {
	int sampling_frequency_index = 3; // 默认使用48000hz采样频率索引
	int adtsLen = data_length + 7; // ADTS帧总长度，包括AAC数据和ADTS头部

	// 查找采样频率对应的索引值
	int i = 0;
	for (i; i < sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]); i++) {
		if (sampling_frequencies[i] == samplerate) {
			sampling_frequency_index = i;
			break;
		}
	}
	if (i >= sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0])) {
		printf("unsupport samplerate:%d\n", samplerate);
		return -1; // 如果没有找到对应的采样频率，返回错误
	}

	// 填充ADTS头部的固定字段
	p_adts_header[0] = 0xff; // syncword高8位
	p_adts_header[1] = 0xf0; // syncword低4位，MPEG版本（这里为MPEG-4 AAC），Layer（AAC是Layer 1）
	p_adts_header[1] |= (0 << 3);//1bit
	p_adts_header[1] |= (0 << 1);// 2bits
	p_adts_header[1] |= 1; // protection_absent: 表示没有CRC校验// 1bits

	// 填充ADTS头部的可变字段
	p_adts_header[2] = (profile) << 6; // profile字段 // 2bits
	p_adts_header[2] |= (sampling_frequency_index & 0x0f) << 2; // sampling_frequency_index字段  4bits
	p_adts_header[2] |= (0 << 1); // private_bit: 保留位 1bits
	p_adts_header[2] |= (channels & 0x04) >> 2; // channel_configuration字段高1位
	p_adts_header[3] = (channels & 0x03) << 6; // channel_configuration字段低2位
	p_adts_header[3] |= (0 << 5); // original: 保留位 1bit
	p_adts_header[3] |= (0 << 4); // home: 保留位 1bit
	p_adts_header[3] |= (0 << 3); // copyright id bit: 保留位 1bit
	p_adts_header[3] |= (0 << 2); // copyright id start: 保留位 1bit
	p_adts_header[3] |= ((adtsLen & 0x1800) >> 11); // frame_length字段高2位
	p_adts_header[4] = (uint8_t)((adtsLen & 0x7f8) >> 3); // frame_length字段中间8位
	p_adts_header[5] = (uint8_t)((adtsLen & 0x7) << 5); // frame_length字段低3位
	p_adts_header[5] |= 0x1f; // buffer fullness: 0x7ff，表示码率可变
	p_adts_header[6] = 0xfc; // buffer fullness: 0x7ff剩余的6位

	return 0; // 成功返回0
}


int getAACStream(MP4FileHandle mp4Handler, int audioTrackId, int totalSamples, const char* saveFileName)
{
	// 调用的接口要传的参数
	uint32_t curFrameIndex = 1; // `MP4ReadSample`函数的参数要求是从1开始，但我们打印帧下标还是从0开始
	uint8_t* pData = NULL;
	uint32_t nSize = 0;

	MP4Timestamp pStartTime;
	MP4Duration pDuration;
	MP4Duration pRenderingOffset;
	bool pIsSyncSample = 0;

	// 写文件要用的参数
	FILE* fpAudio = NULL;

	if (!mp4Handler)
		return -1;

	fpAudio = fopen(saveFileName, "wb");
	if (fpAudio == NULL)
	{
		printf("open file(%s) error!\n", saveFileName);
		return -1;
	}

	int64_t msectime = 0;

	using clock = std::chrono::high_resolution_clock;
	using milliseconds = std::chrono::milliseconds;
	auto startde = clock::now();

	auto audioSampleCnt = MP4GetTrackNumberOfSamples(mp4Handler, audioTrackId);
	//音频格式类型
	auto audio_type = MP4GetTrackAudioMpeg4Type(mp4Handler, audioTrackId);
	//声道数
	auto audio_channel_num = MP4GetTrackAudioChannels(mp4Handler, audioTrackId);
	//音频编码
	const char* audio_name = MP4GetTrackMediaDataName(mp4Handler, audioTrackId);
	// 采样率
	auto audio_time_scale = MP4GetTrackTimeScale(mp4Handler, audioTrackId);

	uint32_t		timescale = MP4GetTrackTimeScale(mp4Handler, audioTrackId);

	uint8_t* ppConfig = nullptr;
	uint32_t pConfigSize = 0;

	uint32_t old_pts = 0;
	MP4GetTrackESConfiguration(mp4Handler, audioTrackId, &ppConfig, &pConfigSize);
	std::string strAES;
	if (strAES.size() <= 0 && pConfigSize > 0) {
		strAES.assign((char*)ppConfig, pConfigSize);
	}
	while (curFrameIndex <= totalSamples)
	{
		// 如果传入MP4ReadSample的音频pData是null，它内部就会new 一个内存
		// 如果传入的是已知的内存区域，则需要保证空间bigger then max frames size.
		MP4ReadSample(mp4Handler, audioTrackId, curFrameIndex, &pData, &nSize, &pStartTime, &pDuration, &pRenderingOffset, &pIsSyncSample);
		if (curFrameIndex == 4196) {
			int i = 0;
		}
		//计算发送时间 => PTS => 刻度时间转换成毫秒..
		msectime = MP4GetSampleTime(mp4Handler, audioTrackId, curFrameIndex);
		msectime *= UINT64_C(1000);
		//msectime *= 2;
		msectime /= timescale;
		if (old_pts > msectime) {
			int i = 0;
		}
		old_pts = msectime;

		uint64_t ptss = curFrameIndex * (1000 / audio_time_scale);
		//本地时间
		auto endde = clock::now();
		auto now = std::chrono::system_clock::now();
		// 转换为 time_t，适用于 std::ctime 函数族
		std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
		// 使用 localtime 获取本地时间
		std::tm* local_tm = std::localtime(&now_time_t);
		// 使用 std::put_time 将 tm 结构体转换为字符串形式
		/*std::cout << "curFrameIndex:"<< curFrameIndex << "   Current time: "
			<< std::put_time(local_tm, "%Y-%m-%d %H:%M:%S") << "; play startTime: " << msectime << std::endl;*/

		//auto durationde = std::chrono::duration_cast<milliseconds>(endde - startde);
		//int64_t st = msectime - durationde.count();
		//st = st > 0 ? st : 1;
		//std::this_thread::sleep_for(std::chrono::milliseconds(st));

		//DEBUG("[\033[36maudio\033[0m] ");

		//printf("play startTime: %d\n", pStartTime);

		if (pData)
		{
			uint8_t adts_header_buf[7] = { 0 };
			adts_header(adts_header_buf, nSize,
				1,
				audio_time_scale,
				audio_channel_num);

			fwrite(adts_header_buf, 7, 1, fpAudio);
			//DEBUG("frame index: %d\t size: %d\n", curFrameIndex - 1, nSize);
			fwrite(pData, nSize, 1, fpAudio);
		}

		free(pData);
		pData = NULL;

		curFrameIndex++;
	}
	fflush(fpAudio);
	fclose(fpAudio);

	return 0;
}

void demuxMp4(MP4FileHandle _mp4Handler, int _audioTrackId, int _videoTrackId)
{
	auto videoSampleCnt = MP4GetTrackNumberOfSamples(_mp4Handler, _videoTrackId);
	uint32_t audioSampleCnt = 0;
	if (_audioTrackId > 0) {
		audioSampleCnt = MP4GetTrackNumberOfSamples(_mp4Handler, _audioTrackId);
	}

	uint8_t* pData = nullptr;
	MP4Timestamp pStartTime;
	MP4Duration pDuration;
	MP4Duration pRenderingOffset;
	bool pIsSyncSample = false;
	char naluHeader[4] = { 0x00, 0x00, 0x00, 0x01 };

	uint32_t		timescale = MP4GetTrackTimeScale(_mp4Handler, _videoTrackId);
	uint32_t		audio_timescale = MP4GetTrackTimeScale(_mp4Handler, _audioTrackId);
	uint64_t		msectime = 0;

	//当前解码最新pts
	uint32_t video_pts = 0;
	uint32_t audio_pts = 0;
	int32_t sleep_timer = 0;

	using clock = std::chrono::high_resolution_clock;
	using milliseconds = std::chrono::milliseconds;
	auto startTime = clock::now();

	// 这个api要求从1开始的
	uint32_t curVideoIndex = 1;
	uint32_t curAudioIndex = 1;
	uint32_t   nSize = 0;
	while (curVideoIndex <= videoSampleCnt && curAudioIndex <= audioSampleCnt) {
		auto dur_time = std::chrono::duration_cast<milliseconds>(clock::now() - startTime);
		uint32_t local_time = dur_time.count();

		//保持解析pts最多比本地pts大2000毫秒
		if (curVideoIndex <= videoSampleCnt && (video_pts <= local_time || (video_pts >= local_time && video_pts - local_time < 2000))) {
			// 如果传入MP4ReadSample的视频pData是null，它内部就会new 一个内存
			// 如果传入的是已知的内存区域，则需要保证空间足够大
			bool ret = MP4ReadSample(_mp4Handler, _videoTrackId, curVideoIndex, &pData, &nSize, &pStartTime, &pDuration, &pRenderingOffset, &pIsSyncSample);
			if (ret) {
				// pts
				video_pts = MP4GetSampleTime(_mp4Handler, _videoTrackId, curVideoIndex);
				video_pts *= UINT64_C(1000);
				video_pts /= timescale;

				std::cout << "cur video pts: " << video_pts << std::endl;
				// 当前帧展示时间
				auto dur = pDuration * 1000 / timescale;

				if (pIsSyncSample) {
					int i = 0;
					//I帧需要拷贝sps及pps
					//naluHeader+sps+naluHeader+pps
				}
				// naluHeader+pData
			}
			free(pData);
			pData = NULL;
			curVideoIndex++;
		}

		if (curAudioIndex <= audioSampleCnt && (audio_pts <= local_time || (audio_pts >= local_time && audio_pts - local_time < 2000))) {
			bool ret = MP4ReadSample(_mp4Handler, _audioTrackId, curAudioIndex, &pData, &nSize, &pStartTime, &pDuration, nullptr, nullptr);
			if (ret) {
				audio_pts = MP4GetSampleTime(_mp4Handler, _audioTrackId, curAudioIndex);
				audio_pts *= UINT64_C(1000);
				audio_pts /= audio_timescale;

				std::cout << "cur audio pts: " << audio_pts << std::endl;
				//拷贝 pData nSize
			}

			free(pData);
			pData = NULL;
			curAudioIndex++;
		}

		sleep_timer = video_pts - local_time - 2000;
		std::cout << "local_time : " << local_time << "; video_pts:" << video_pts << "; sleep_timer:" << sleep_timer << std::endl;
		sleep_timer = sleep_timer <= (audio_pts - local_time - 2000) ? sleep_timer : (audio_pts - local_time - 2000);
		if (sleep_timer > 0) {
			std::cout << "sleep : " << sleep_timer << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(sleep_timer));
		}
	}
	MP4Close(_mp4Handler, 0);
}


int unpackMp4File()
{
	MP4FileHandle mp4Handler = 0;
	uint32_t trackCnt = 0;
	int videoTrackId = -1;
	int audioTrackId = -1;
	unsigned int videoSampleCnt = 0;
	unsigned int audioSampleCnt = 0;

	//std::string("E:/avfile/test1.mp4");, std::string("E:/avfile/test1.h264").c_str(), std::string("E:/avfile/test1.aac").c_str()
	const char* mp4FileName = "E:/avfile/71196.mp4";
	const char* videoFileName = "E:/avfile/flow.h264";
	const char* audioFileName = "E:/avfile/flow.aac";


	mp4Handler = MP4Read(mp4FileName);
	/*if (mp4Handler <= 0)
	{
		printf("MP4Read(%s) error!\n", mp4FileName);
		return -1;
	}*/

	trackCnt = MP4GetNumberOfTracks(mp4Handler, NULL, 0); //获取音视频轨道数
	printf("****************************\n");
	printf("trackCnt: %d\n", trackCnt);

	char* info = MP4FileInfo(mp4FileName);

	uint32_t dwFileScale = MP4GetTimeScale(mp4Handler);
	MP4Duration theDuration = MP4GetDuration(mp4Handler);
	// 总毫秒数
	uint32_t dwMP4Duration = theDuration * 1000 / dwFileScale;


	auto pspFile = fopen("E:/avfile/sps.h264", "wb");
	if (pspFile == NULL)
	{
		return -1;
	}

	for (int i = 0; i < trackCnt; i++)
	{
		// 获取trackId，判断获取数据类型: 1-获取视频数据，2-获取音频数据
		MP4TrackId trackId = MP4FindTrackId(mp4Handler, i, NULL, 0);
		const char* trackType = MP4GetTrackType(mp4Handler, trackId);

		if (!strcmp(MP4_VIDEO_TRACK_TYPE, trackType))//MP4_IS_VIDEO_TRACK_TYPE(trackType)
		{
			// 不关心，只是打印出来看看
			MP4Duration duration = 0;
			uint32_t timescale = 0;
			MP4Timestamp msectime;

			videoTrackId = trackId;

			//视频编码
			const char* video_name = MP4GetTrackMediaDataName(mp4Handler, videoTrackId);

			uint32_t width = MP4GetTrackVideoWidth(mp4Handler, videoTrackId);
			uint32_t height = MP4GetTrackVideoHeight(mp4Handler, videoTrackId);
			double frame_rate = MP4GetTrackVideoFrameRate(mp4Handler, videoTrackId);

			duration = MP4GetTrackDuration(mp4Handler, videoTrackId);
			timescale = MP4GetTrackTimeScale(mp4Handler, videoTrackId);
			videoSampleCnt = MP4GetTrackNumberOfSamples(mp4Handler, videoTrackId);

			msectime = MP4GetSampleTime(mp4Handler, videoTrackId, videoSampleCnt);

			printf("video params: \n"
				" - trackId: %d\n"
				" - duration: %lu\n"
				" - timescale: %d\n"
				" - samples count: %d\n",
				videoTrackId, duration, timescale, videoSampleCnt);

			// 读取 sps/pps 
			uint8_t** seqheader, ** pictheader;
			uint32_t* pictheadersize, * seqheadersize;
			uint32_t ix;

			MP4GetTrackH264SeqPictHeaders(mp4Handler, trackId,
				&seqheader, &seqheadersize,
				&pictheader, &pictheadersize);
			for (ix = 0; seqheadersize[ix] != 0; ix++) {
				memcpy(g_sps, seqheader[ix], seqheadersize[ix]);
				g_spslen = seqheadersize[ix];
				free(seqheader[ix]);
			}
			free(seqheader);
			free(seqheadersize);

			/*if (g_sps[0] != 0x67) {
				g_sps[0] = 0x67;
			}*/

			fwrite(g_sps, g_spslen,1,pspFile);
			fclose(pspFile);

			// 获取pps
			for (int ix = 0; pictheader[ix] != 0; ix++)
			{
				memcpy(g_pps, pictheader[ix], pictheadersize[ix]);
				g_ppslen = pictheadersize[ix];
				free(pictheader[ix]);
			}
			free(pictheader);
			free(pictheadersize);

			/*if (g_pps[0] != 0x68) {
				g_pps[0] = 0x68;
			}*/
		}
		else if (!strcmp(trackType, MP4_AUDIO_TRACK_TYPE)/*MP4_IS_AUDIO_TRACK_TYPE(trackType)*/)
		{
			audioTrackId = trackId;

			//TRACE("=== 音频轨道号：%d，标识：%s ===\n", tidVideo, type);
			char* lpAudioInfo = MP4Info(mp4Handler, audioTrackId);
			//TRACE("音频信息：%s \n", lpAudioInfo);
			free(lpAudioInfo);

			audioSampleCnt = MP4GetTrackNumberOfSamples(mp4Handler, audioTrackId);

			auto audio_type = MP4GetTrackAudioMpeg4Type(mp4Handler, audioTrackId);
			std::cout << "=== 音频格式类型: ===" << audio_type << std::endl;
			auto audio_channel_num = MP4GetTrackAudioChannels(mp4Handler, audioTrackId);
			//TRACE("=== 音频声道数：%d ===\n", audio_channel_num);
			const char* audio_name = MP4GetTrackMediaDataName(mp4Handler, audioTrackId);
			//TRACE("=== 音频编码：%s ===\n", audio_name);
			auto audio_time_scale = MP4GetTrackTimeScale(mp4Handler, audioTrackId);
			//TRACE("=== 音频每秒刻度数(采样率)：%lu ===\n", audio_time_scale);
		
			std::string strAES;
			// 获取音频扩展信息...
			uint8_t* pAES = NULL;
			uint32_t   nSize = 0;
			bool haveEs = MP4GetTrackESConfiguration(mp4Handler, audioTrackId, &pAES, &nSize);
			// 存储音频扩展信息...
			if (strAES.size() <= 0 && nSize > 0) {
				strAES.assign((char*)pAES, nSize);
			}
			//TRACE("=== 音频扩展信息长度：%lu ===\n", nSize);


			printf("audio params: \n"
				" - trackId: %d\n"
				" - samples count: %d\n",
				audioTrackId, audioSampleCnt);
		}
	}
	printf("****************************\n");

	//demuxMp4(mp4Handler, audioTrackId, videoTrackId);

	// 解析完了mp4，主要是为了获取sps pps 还有video的trackID
	if (videoTrackId >= 0)
	{
		getH264Stream(mp4Handler, videoTrackId, videoSampleCnt, videoFileName);
	}

	if (audioTrackId >= 0)
	{
		getAACStream(mp4Handler, audioTrackId, audioSampleCnt, audioFileName);
	}

	// 需要mp4close 否则在嵌入式设备打开mp4上多了会内存泄露挂掉.
	MP4Close(mp4Handler, 0);

	return 0;
}

