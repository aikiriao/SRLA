#include "srla_player.h"
#include <assert.h>

#include <AudioToolbox/AudioQueue.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <CoreFoundation/CFRunLoop.h>

#define NUM_BUFFERS 3
#define BUFFER_SIZE (8 * 1024)
#define DECODE_BUFFER_NUM_SAMPLES (1024)

/* CoreAudioの出力コールバック関数 */
static void SRLAPlayer_CoreAudioCallback(void *custom_data, AudioQueueRef queue, AudioQueueBufferRef buffer);

/* 初期化カウント */
static int32_t st_initialize_count = 0;
/* 初期化時のプレイヤーコンフィグ */
static struct SRLAPlayerConfig st_config = { 0, };
/* デコードしたデータのバッファ領域 */
static int32_t **st_decode_buffer = NULL;
/* バッファ参照位置 */
static uint32_t st_buffer_pos = DECODE_BUFFER_NUM_SAMPLES; /* 空の状態 */
/* 出力キューの参照 */
static AudioQueueRef queue = NULL;

/* 初期化 この関数内でデバイスドライバの初期化を行い、再生開始 */
void SRLAPlayer_Initialize(const struct SRLAPlayerConfig *config)
{
    uint32_t i;
    AudioStreamBasicDescription format;
    AudioQueueBufferRef buffers[NUM_BUFFERS];

    assert(config != NULL);

    /* 多重初期化は不可 */
    if (st_initialize_count > 0) {
        return;
    }

    /* コンフィグ取得 */
    st_config = (*config);

    /* フォーマットに属性を詰める */
    format.mSampleRate       = st_config.sampling_rate; /* サンプリングレート */
    format.mFormatID         = kAudioFormatLinearPCM; /* フォーマット: PCM */
    format.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked; /* フォーマットフラグの指定. */
    format.mBitsPerChannel   = st_config.bits_per_sample; /* チャンネル当たりのビット数 */
    format.mChannelsPerFrame = st_config.num_channels; /* チャンネル数 */
    format.mBytesPerFrame    = (st_config.bits_per_sample * st_config.num_channels) / 8; /* 1フレーム（全てのフレームの1サンプル）のバイト数 */
    format.mFramesPerPacket  = 1; /* パケットあたりのフレーム数 */
    format.mBytesPerPacket   = format.mBytesPerFrame * format.mFramesPerPacket; /* パケット当たりのバイト数 */
    format.mReserved         = 0; /* （予約領域） */

    /* デコード領域のバッファ確保 */
    st_decode_buffer = (int32_t **)malloc(sizeof(int32_t *) * st_config.num_channels);
    for (i = 0; i < st_config.num_channels; i++) {
        st_decode_buffer[i] = (int32_t *)malloc(sizeof(int32_t) * DECODE_BUFFER_NUM_SAMPLES);
        memset(st_decode_buffer[i], 0, sizeof(int32_t) * DECODE_BUFFER_NUM_SAMPLES);
    }

    /* 新しい出力キューを生成 */
    AudioQueueNewOutput(&format,
            SRLAPlayer_CoreAudioCallback, NULL, CFRunLoopGetCurrent(), kCFRunLoopCommonModes, 0, &queue);

    for (i = 0; i < NUM_BUFFERS; i++) {
        /* 指定したキューのバッファの領域を割り当てる */
        AudioQueueAllocateBuffer(queue, BUFFER_SIZE, &buffers[i]);
        /* サイズをセット */
        buffers[i]->mAudioDataByteSize = BUFFER_SIZE;
        /* 一発目のデータを出力 */
        SRLAPlayer_CoreAudioCallback(NULL, queue, buffers[i]);
    }

    /* キューの再生開始 */
    AudioQueueStart(queue, NULL);

    /* スレッドのループ処理開始
    * この関数が終わってもスレッド処理が回る（監視ループというらしい） */
    CFRunLoopRun();

    st_initialize_count++;
}

/* 終了 初期化したときのリソースの開放はここで */
void SRLAPlayer_Finalize(void)
{
    if (st_initialize_count == 1) {
        uint32_t i;
        /* キューの停止・破棄 */
        AudioQueueStop(queue, false);
        AudioQueueDispose(queue, false);
        CFRunLoopStop(CFRunLoopGetCurrent());

        /* デコード領域のバッファ開放 */
        for (i = 0; i < st_config.num_channels; i++) {
            free(st_decode_buffer[i]);
        }
        free(st_decode_buffer);
    }

    st_initialize_count--;
}

/* CoreAudioの出力コールバック関数 */
static void SRLAPlayer_CoreAudioCallback(void *custom_data, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    uint32_t i, ch;
    int16_t *ch_interleaved_buffer = (int16_t *)buffer->mAudioData;
    const uint32_t num_buffer_samples = BUFFER_SIZE / sizeof(int16_t);

    for (i = 0; i < num_buffer_samples; i += st_config.num_channels) {
        /* バッファを使い切っていたらその場で次のデータを要求 */
        if (st_buffer_pos >= DECODE_BUFFER_NUM_SAMPLES) {
            st_config.sample_request_callback(st_decode_buffer, st_config.num_channels, DECODE_BUFFER_NUM_SAMPLES);
            st_buffer_pos = 0;
        }

        /* インターリーブしたバッファにデータを詰める */
        for (ch = 0; ch < st_config.num_channels; ch++) {
            ch_interleaved_buffer[i + ch] = (int16_t)st_decode_buffer[ch][st_buffer_pos];
        }
        st_buffer_pos++;
    }

    /* バッファをエンキュー */
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}
