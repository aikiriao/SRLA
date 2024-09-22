#include "srla_player.h"
#include <assert.h>
#include <stdio.h>

#include <windows.h>

/* マクロ呼び出しを使うためCOBJMACROSを定義 */
#define COBJMACROS
#include <mmdeviceapi.h>
#include <audioclient.h>
#undef COBJMACROS

#define DECODE_BUFFER_NUM_SAMPLES (1024)
#define REQUESTED_SOUND_BUFFER_DURATION  (2 * 10000000LL) /* 内部に要求するバッファサイズ[100ナノ秒] */

/* 初期化カウント */
static int32_t st_initialize_count = 0;
/* 初期化時のプレイヤーコンフィグ */
static struct SRLAPlayerConfig st_config = { 0, };
/* デコードしたデータのバッファ領域 */
static int32_t** st_decode_buffer = NULL;
/* バッファ参照位置 */
static uint32_t st_buffer_pos = DECODE_BUFFER_NUM_SAMPLES; /* 空の状態 */
/* WASAPI制御用のハンドル */
static IAudioClient* audio_client = NULL;
static IAudioRenderClient* audio_render_client = NULL;

/* CLSID,IIDを自前定義 */
/* 補足）C++ソースにしないと__uuidが使えない。C++にするならクラスを使う。しかしwindowsの事情だけで全てをC++プロジェクトにしたくない */
static const CLSID st_CLSID_MMDeviceEnumerator = { 0xBCDE0395, 0xE52F, 0x467C, {0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E} };
static const IID st_IID_IMMDeviceEnumerator = { 0xA95664D2, 0x9614, 0x4F35, {0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6} };
static const IID st_IID_IAudioClient = { 0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2} };
static const IID st_IID_IAudioClockAdjustment = { 0xF6E4C0A0, 0x46D9, 0x4FB8, {0xBE,0x21,0x57,0xA3,0xEF,0x2B,0x62,0x6C} };
static const IID st_IID_IAudioRenderClient = { 0xF294ACFC, 0x3146, 0x4483, {0xA7,0xBF,0xAD,0xDC,0xA7,0xC2,0x60,0xE2} };

/* 初期化 この関数内でデバイスドライバの初期化を行い、再生開始 */
void SRLAPlayer_Initialize(const struct SRLAPlayerConfig* config)
{
    uint32_t i, buffer_frame_size;
    HRESULT hr;
    IMMDeviceEnumerator* device_enumerator;
    IMMDevice* audio_device;
    WAVEFORMATEX format;

    assert(config != NULL);

    /* 多重初期化は不可 */
    if (st_initialize_count > 0) {
        return;
    }

    /* コンフィグ取得 */
    st_config = (*config);

    /* デコード領域のバッファ確保 */
    st_decode_buffer = (int32_t**)malloc(sizeof(int32_t*) * st_config.num_channels);
    for (i = 0; i < st_config.num_channels; i++) {
        st_decode_buffer[i] = (int32_t*)malloc(sizeof(int32_t) * DECODE_BUFFER_NUM_SAMPLES);
        memset(st_decode_buffer[i], 0, sizeof(int32_t) * DECODE_BUFFER_NUM_SAMPLES);
    }

    /* COMの初期化 */
    hr = CoInitializeEx(NULL, COINIT_SPEED_OVER_MEMORY);
    assert(SUCCEEDED(hr));

    /* マルチメディアデバイス列挙子取得 */
    hr = CoCreateInstance(&st_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &st_IID_IMMDeviceEnumerator, &device_enumerator);
    assert(SUCCEEDED(hr));

    /* デフォルトのデバイス取得 */
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(device_enumerator, eRender, eConsole, &audio_device);
    assert(SUCCEEDED(hr));
    IMMDeviceEnumerator_Release(device_enumerator);

    /* クライアント取得 */
    hr = IMMDevice_Activate(audio_device, &st_IID_IAudioClient, CLSCTX_ALL, NULL, &audio_client);
    assert(SUCCEEDED(hr));
    IMMDevice_Release(audio_device);

    /* 出力フォーマット指定 */
    ZeroMemory(&format, sizeof(WAVEFORMATEX));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = st_config.num_channels;
    format.nSamplesPerSec = st_config.sampling_rate;
    format.wBitsPerSample = st_config.bits_per_sample;
    format.nBlockAlign = (format.nChannels * format.wBitsPerSample) / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    /* 出力フォーマットが対応しているかチェック */
    {
        WAVEFORMATEX closest_format, *pformat;
        pformat = &closest_format;
        hr = IAudioClient_IsFormatSupported(audio_client, AUDCLNT_SHAREMODE_SHARED, &format, &pformat);
        if (hr != S_OK) {
            fprintf(stderr, "Unsupported format for WASAPI Playback. \n");
            exit(1);
        }
    }

    /* クライアント初期化 */
    hr = IAudioClient_Initialize(audio_client,
            AUDCLNT_SHAREMODE_SHARED, /* 共有モード */
            AUDCLNT_STREAMFLAGS_RATEADJUST /* レート変換を使う（入力波形に合わせたレートで再生する） */
            | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM /* レート変換の自動挿入を有効にする */
            | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, /* 良い品質のレート変換を使う */
            REQUESTED_SOUND_BUFFER_DURATION, 0, &format, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to initialize WASAPI client. \n");
        exit(2);
    }

    /* サンプリングレート変換設定 */
    {
        IAudioClockAdjustment* clock_adj;
        hr = IAudioClient_GetService(audio_client, &st_IID_IAudioClockAdjustment, &clock_adj);
        assert(SUCCEEDED(hr));

        hr = IAudioClockAdjustment_SetSampleRate(clock_adj, st_config.sampling_rate);
        assert(SUCCEEDED(hr));
        IAudioClockAdjustment_Release(clock_adj);
    }

    /* バッファを書き込む為のレンダラーを取得 */
    hr = IAudioClient_GetService(audio_client, &st_IID_IAudioRenderClient, &audio_render_client);
    assert(SUCCEEDED(hr));

    /* 書き込み用のバッファサイズ取得 */
    hr = IAudioClient_GetBufferSize(audio_client, &buffer_frame_size);
    assert(SUCCEEDED(hr));

    /* 再生開始 */
    hr = IAudioClient_Start(audio_client);
    assert(SUCCEEDED(hr));

    st_initialize_count++;

    while (1) {
        int16_t* buffer;
        /* レイテンシ: 小さすぎると途切れる, 大きすぎると遅延が大きくなる */
        const uint32_t buffer_latency = buffer_frame_size / 50;
        uint32_t padding_size, available_buffer_frame_size;

        /* パディングフレームサイズ（サウンドバッファ内に入っていてまだ出力されてないデータ量）の取得 */
        hr = IAudioClient_GetCurrentPadding(audio_client, &padding_size);
        assert(SUCCEEDED(hr));

        /* 書き込み可能なフレームサイズの取得 */
        available_buffer_frame_size = buffer_latency - padding_size;

        /* 書き込み用バッファ取得 */
        hr = IAudioRenderClient_GetBuffer(audio_render_client, available_buffer_frame_size, &buffer);
        assert(SUCCEEDED(hr));

        /* インターリーブしつつ書き込み チャンネル数分のサンプルのまとまりが1フレーム */
        for (i = 0; i < available_buffer_frame_size; i++) {
            uint32_t ch;
            /* バッファを使い切っていたらその場で次のデータを要求 */
            if (st_buffer_pos >= DECODE_BUFFER_NUM_SAMPLES) {
                st_config.sample_request_callback(st_decode_buffer, st_config.num_channels, DECODE_BUFFER_NUM_SAMPLES);
                st_buffer_pos = 0;
            }

            /* インターリーブしたバッファにデータを詰める */
            for (ch = 0; ch < st_config.num_channels; ch++) {
                *buffer++ = (int16_t)st_decode_buffer[ch][st_buffer_pos];
            }
            st_buffer_pos++;
        }

        /* バッファの解放 */
        hr = IAudioRenderClient_ReleaseBuffer(audio_render_client, available_buffer_frame_size, 0);
        assert(SUCCEEDED(hr));
    }
}

/* 終了 初期化したときのリソースの開放はここで */
void SRLAPlayer_Finalize(void)
{
    if (st_initialize_count == 1) {
        IAudioClient_Stop(audio_client);
        IAudioClient_Release(audio_client);
        IAudioRenderClient_Release(audio_render_client);
    }

    st_initialize_count--;
}
