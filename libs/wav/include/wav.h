#ifndef WAV_INCLUDED
#define WAV_INCLUDED

#include <stdint.h>

/* PCM型 - ファイルのビット深度如何によらず、メモリ上では全て符号付き32bitで取り扱う */
typedef int32_t WAVPcmData;

/* WAVデータのフォーマット */
typedef enum WAVDataFormatTag {
    WAV_DATA_FORMAT_PCM             /* PCMのみ対応 */
} WAVDataFormat;

/* API結果型 */
typedef enum WAVApiResultTag {
    WAV_APIRESULT_OK = 0,
    WAV_APIRESULT_NG,
    WAV_APIRESULT_INVALID_FORMAT,     /* フォーマットが不正 */
    WAV_APIRESULT_IOERROR,            /* ファイル入出力エラー */
    WAV_APIRESULT_INVALID_PARAMETER   /* 引数が不正 */
} WAVApiResult;

/* WAVファイルフォーマット */
struct WAVFileFormat {
    WAVDataFormat data_format;      /* データフォーマット */
    uint32_t      num_channels;     /* チャンネル数 */
    uint32_t      sampling_rate;    /* サンプリングレート */
    uint32_t      bits_per_sample;  /* 量子化ビット数 */
    uint32_t      num_samples;      /* サンプル数 */
};

/* WAVファイルハンドル */
struct WAVFile {
    struct WAVFileFormat  format;   /* フォーマット */
    WAVPcmData**          data;     /* 実データ     */
};

/* アクセサ */
#define WAVFile_PCM(wavfile, samp, ch)  (wavfile->data[(ch)][(samp)])

#ifdef __cplusplus
extern "C" {
#endif

/* ファイルからWAVファイルハンドルを作成 */
struct WAVFile* WAV_CreateFromFile(const char* filename);

/* フォーマットを指定して新規にWAVファイルハンドルを作成 */
struct WAVFile* WAV_Create(const struct WAVFileFormat* format);

/* WAVファイルハンドルを破棄 */
void WAV_Destroy(struct WAVFile* wavfile);

/* ファイル書き出し */
WAVApiResult WAV_WriteToFile(
        const char* filename, const struct WAVFile* wavfile);

/* ファイルからWAVファイルフォーマットだけ読み取り */
WAVApiResult WAV_GetWAVFormatFromFile(
        const char* filename, struct WAVFileFormat* format);

#ifdef __cplusplus
}
#endif

#endif /* WAV_INCLUDED */
