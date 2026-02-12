#ifndef WAV_INCLUDED
#define WAV_INCLUDED

#include <stdint.h>

/* PCM型 - ファイルのビット深度如何によらず、メモリ上では全て符号付き32bitで取り扱う */
typedef int32_t WAVPcmData;

/* WAVファイルフォーマット */
typedef enum {
    WAV_FILEFORMAT_PCMWAVEFORMAT = 0, /* PCMWAVEFORMAT */
    WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE, /* WAVEFORMATEXTENSIBLE */
    WAV_FILEFORMAT_AIFF, /* AIFF */
    WAV_FILEFORMAT_INVALID, /* 無効 */
} WAVFileFormat;

/* API結果型 */
typedef enum {
    WAV_APIRESULT_OK = 0, /* 成功 */
    WAV_APIRESULT_NG, /* 分類不能なエラー */
    WAV_APIRESULT_UNSUPPORTED_FORMAT, /* サポートしていないフォーマット */
    WAV_APIRESULT_INVALID_FORMAT, /* フォーマットが不正 */
    WAV_APIRESULT_IOERROR, /* ファイル入出力エラー */
    WAV_APIRESULT_INVALID_PARAMETER /* 引数が不正 */
} WAVApiResult;

/* WAVフォーマット */
struct WAVFormat {
    WAVFileFormat file_format; /* ファイルフォーマット */
    uint32_t num_channels; /* チャンネル数 */
    uint32_t sampling_rate; /* サンプリングレート */
    uint32_t bits_per_sample; /* 量子化ビット数 */
    uint32_t num_samples; /* サンプル数 */
    union {
        /* WAVEFORMATEXTENSIBLEの拡張情報 */
        struct {
            uint16_t sample_information;
            uint32_t channel_mask;
            char guid[16];
        } wav_extensible;
        struct {
            uint8_t key;
            int16_t gain;
            uint32_t loop_begin;
            uint32_t loop_end;
        } aiff_instrument;
    } u;
};

/* WAVファイルハンドル */
struct WAVFile {
    struct WAVFormat format; /* フォーマット */
    WAVPcmData **data; /* PCM配列 */
};

/* アクセサ */
#define WAVFile_PCM(wavfile, samp, ch)  (wavfile->data[(ch)][(samp)])

#ifdef __cplusplus
extern "C" {
#endif

/* ファイルからWAVファイルハンドルを作成 */
struct WAVFile* WAV_CreateFromFile(const char* filename);

/* フォーマットを指定して新規にWAVファイルハンドルを作成 */
struct WAVFile* WAV_Create(const struct WAVFormat* format);

/* WAVファイルハンドルを破棄 */
void WAV_Destroy(struct WAVFile* wavfile);

/* ファイル書き出し */
WAVApiResult WAV_WriteToFile(
        const char* filename, const struct WAVFile* wavfile);

/* ファイルからWAVファイルフォーマットだけ読み取り */
WAVApiResult WAV_GetWAVFormatFromFile(
        const char* filename, struct WAVFormat* format);

#ifdef __cplusplus
}
#endif

#endif /* WAV_INCLUDED */
