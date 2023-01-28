#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* パーサの読み込みバッファサイズ */
#define WAVBITBUFFER_BUFFER_SIZE         (10 * 1024)

/* 下位n_bitsを取得 */
/* 補足）((1 << n_bits) - 1)は下位の数値だけ取り出すマスクになる */
#define WAV_GetLowerBits(n_bits, val) ((val) & (uint32_t)((1 << (n_bits)) - 1))

/* a,bの内の小さい値を取得 */
#define WAV_Min(a, b) (((a) < (b)) ? (a) : (b))

/* 内部エラー型 */
typedef enum WAVErrorTag {
    WAV_ERROR_OK = 0,             /* OK */
    WAV_ERROR_NG,                 /* 分類不能な失敗 */
    WAV_ERROR_IO,                 /* 入出力エラー */
    WAV_ERROR_INVALID_PARAMETER,  /* 不正な引数 */
    WAV_ERROR_INVALID_FORMAT      /* 不正なフォーマット */
} WAVError;

/* ビットバッファ */
struct WAVBitBuffer {
    uint8_t   bytes[WAVBITBUFFER_BUFFER_SIZE];   /* ビットバッファ */
    uint32_t  bit_count;                        /* ビット入力カウント */
    int32_t   byte_pos;                         /* バイト列読み込み位置 */
};

/* パーサ */
struct WAVParser {
    FILE*               fp;       /* 読み込みファイルポインタ */
    struct WAVBitBuffer buffer;   /* ビットバッファ */
};

/* ライタ */
struct WAVWriter {
    FILE*     fp;                 /* 書き込みファイルポインタ */
    uint32_t  bit_buffer;         /* 出力途中のビット */
    uint32_t  bit_count;          /* 出力カウント     */
    struct WAVBitBuffer buffer;   /* ビットバッファ */
};

/* パーサの初期化 */
static void WAVParser_Initialize(struct WAVParser* parser, FILE* fp);
/* パーサの使用終了 */
static void WAVParser_Finalize(struct WAVParser* parser);
/* n_bit 取得し、結果を右詰めする */
static WAVError WAVParser_GetBits(struct WAVParser* parser, uint32_t n_bits, uint64_t* bitsbuf);
/* シーク（fseek準拠） */
static WAVError WAVParser_Seek(struct WAVParser* parser, int32_t offset, int32_t wherefrom);
/* ライタの初期化 */
static void WAVWriter_Initialize(struct WAVWriter* writer, FILE* fp);
/* ライタの終了 */
static void WAVWriter_Finalize(struct WAVWriter* writer);
/* valの下位n_bitを書き込む */
static WAVError WAVWriter_PutBits(struct WAVWriter* writer, uint64_t val, uint32_t n_bits);
/* バッファにたまったビットをクリア */
static WAVError WAVWriter_Flush(struct WAVWriter* writer);
/* リトルエンディアンでビットパターンを出力 */
static WAVError WAVWriter_PutLittleEndianBytes(
        struct WAVWriter* writer, uint32_t nbytes, uint64_t data);

/* ライタを使用してファイルフォーマットに従ったヘッダ部を出力 */
static WAVError WAVWriter_PutWAVHeader(
        struct WAVWriter* writer, const struct WAVFileFormat* format);
/* ライタを使用してPCMデータ出力 */
static WAVError WAVWriter_PutWAVPcmData(
        struct WAVWriter* writer, const struct WAVFile* wavfile);

/* リトルエンディアンでビットパターンを取得 */
static WAVError WAVParser_GetLittleEndianBytes(
        struct WAVParser* parser, uint32_t nbytes, uint64_t* bitsbuf);
/* パーサを使用して文字列取得 */
static WAVError WAVParser_GetString(
        struct WAVParser* parser, char* string_buffer, uint32_t string_length);
/* パーサを使用して文字列取得/一致チェック */
static WAVError WAVParser_CheckSignatureString(
        struct WAVParser* parser, const char* signature, uint32_t signature_length);
/* パーサを使用してファイルフォーマットを読み取り */
static WAVError WAVParser_GetWAVFormat(
        struct WAVParser* parser, struct WAVFileFormat* format);
/* パーサを使用してPCMデータを読み取り */
static WAVError WAVParser_GetWAVPcmData(
        struct WAVParser* parser, struct WAVFile* wavfile);

/* 8bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert8bitPCMto32bitPCM(int32_t in_8bitpcm);
/* 16bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert16bitPCMto32bitPCM(int32_t in_16bitpcm);
/* 24bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert24bitPCMto32bitPCM(int32_t in_24bitpcm);
/* 32bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert32bitPCMto32bitPCM(int32_t in_32bitpcm);

/* 32bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert32bitPCMto32bitPCM(int32_t in_32bitpcm);

/* パーサを使用してファイルフォーマットを読み取り */
static WAVError WAVParser_GetWAVFormat(
        struct WAVParser* parser, struct WAVFileFormat* format)
{
    uint64_t  bitsbuf;
    int32_t   fmt_chunk_size;
    struct WAVFileFormat tmp_format;

    /* 引数チェック */
    if (parser == NULL || format == NULL) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* ヘッダ 'R', 'I', 'F', 'F' をチェック */
    if (WAVParser_CheckSignatureString(parser, "RIFF", 4) != WAV_ERROR_OK) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* ファイルサイズ-8（読み飛ばし） */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ヘッダ 'W', 'A', 'V', 'E' をチェック */
    if (WAVParser_CheckSignatureString(parser, "WAVE", 4) != WAV_ERROR_OK) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* fmtチャンクのヘッダ 'f', 'm', 't', ' ' をチェック */
    if (WAVParser_CheckSignatureString(parser, "fmt ", 4) != WAV_ERROR_OK) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* fmtチャンクのバイト数を取得
    * 補足/注意）16より大きいサイズのfmtチャンクの内容（拡張）は読み飛ばす */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    fmt_chunk_size = (int32_t)bitsbuf;

    /* フォーマットIDをチェック
    * 補足）1（リニアPCM）以外対応していない */
    if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (bitsbuf != 1) {
        /* fprintf(stderr, "Unsupported format: fmt chunk format ID \n"); */
        return WAV_ERROR_INVALID_FORMAT;
    }
    tmp_format.data_format = WAV_DATA_FORMAT_PCM;

    /* チャンネル数 */
    if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    tmp_format.num_channels = (uint32_t)bitsbuf;

    /* サンプリングレート */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    tmp_format.sampling_rate =(uint32_t) bitsbuf;

    /* データ速度（byte/sec）は読み飛ばし */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ブロックあたりサイズ数は読み飛ばし */
    if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* 量子化ビット数（サンプルあたりのビット数） */
    if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    tmp_format.bits_per_sample = (uint32_t)bitsbuf;

    /* 拡張部分の読み取りには未対応: 読み飛ばしを行う */
    if (fmt_chunk_size > 16) {
        fprintf(stderr, "Warning: skip fmt chunk extention (unsupported). \n");
        if (WAVParser_Seek(parser, fmt_chunk_size - 16, SEEK_CUR) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    }

    /* チャンク読み取り */
    while (1) {
        char string_buf[4];
        /* チャンク文字列取得 */
        if (WAVParser_GetString(parser, string_buf, 4) != WAV_ERROR_OK) {
            return WAV_ERROR_IO;
        }
        if (strncmp(string_buf, "data", 4) == 0) {
            /* データチャンクを見つけたら終わり */
            break;
        } else {
            /* 他のチャンクはサイズだけ取得してシークにより読み飛ばす */
            if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) {
                return WAV_ERROR_IO;
            }
            /* printf("chunk:%s size:%d \n", string_buf, (int32_t)bitsbuf); */
            WAVParser_Seek(parser, (int32_t)bitsbuf, SEEK_CUR);
        }
    }

    /* サンプル数: 波形データバイト数から算出 */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    tmp_format.num_samples = (uint32_t)bitsbuf;
    assert(tmp_format.num_samples % ((tmp_format.bits_per_sample / 8) * tmp_format.num_channels) == 0);
    tmp_format.num_samples /= ((tmp_format.bits_per_sample / 8) * tmp_format.num_channels);

    /* 構造体コピー */
    *format = tmp_format;

    return WAV_ERROR_OK;
}

/* パーサを使用してPCMデータを読み取り */
static WAVError WAVParser_GetWAVPcmData(
        struct WAVParser* parser, struct WAVFile* wavfile)
{
    uint32_t  ch, sample, bytes_per_sample;
    uint64_t  bitsbuf;
    int32_t   (*convert_to_sint32_func)(int32_t);

    /* 引数チェック */
    if (parser == NULL || wavfile == NULL) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* ビット深度に合わせてPCMデータの変換関数を決定 */
    switch (wavfile->format.bits_per_sample) {
    case 8:
        convert_to_sint32_func = WAV_Convert8bitPCMto32bitPCM;
        break;
    case 16:
        convert_to_sint32_func = WAV_Convert16bitPCMto32bitPCM;
        break;
    case 24:
        convert_to_sint32_func = WAV_Convert24bitPCMto32bitPCM;
        break;
    case 32:
        convert_to_sint32_func = WAV_Convert32bitPCMto32bitPCM;
        break;
    default:
        /* fprintf(stderr, "Unsupported bits per sample format(=%d). \n", wavfile->format.bits_per_sample); */
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* データ読み取り */
    bytes_per_sample = wavfile->format.bits_per_sample / 8;
    for (sample = 0; sample < wavfile->format.num_samples; sample++) {
        for (ch = 0; ch < wavfile->format.num_channels; ch++) {
            if (WAVParser_GetLittleEndianBytes(parser, bytes_per_sample, &bitsbuf) != WAV_ERROR_OK) {
                return WAV_ERROR_IO;
            }
            /* 32bit整数形式に変形してデータにセット */
            wavfile->data[ch][sample] = convert_to_sint32_func((int32_t)(bitsbuf));
        }
    }

    return WAV_ERROR_OK;
}

/* ファイルからWAVファイルフォーマットだけ読み取り */
WAVApiResult WAV_GetWAVFormatFromFile(
        const char* filename, struct WAVFileFormat* format)
{
    struct WAVParser parser;
    FILE*            fp;

    /* 引数チェック */
    if (filename == NULL || format == NULL) {
        return WAV_APIRESULT_NG;
    }

    /* wavファイルを開く */
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        /* fprintf(stderr, "Failed to open %s. \n", filename); */
        return WAV_APIRESULT_NG;
    }

    /* パーサ初期化 */
    WAVParser_Initialize(&parser, fp);

    /* ヘッダ読み取り */
    if (WAVParser_GetWAVFormat(&parser, format) != WAV_ERROR_OK) {
        return WAV_APIRESULT_NG;
    }

    /* パーサ使用終了 */
    WAVParser_Finalize(&parser);

    /* ファイルを閉じる */
    fclose(fp);

    return WAV_APIRESULT_OK;
}

/* ファイルからWAVファイルハンドルを作成 */
struct WAVFile* WAV_CreateFromFile(const char* filename)
{
    struct WAVParser      parser;
    FILE*                 fp;
    struct WAVFile*       wavfile;
    struct WAVFileFormat  format;

    /* 引数チェック */
    if (filename == NULL) {
        return NULL;
    }

    /* wavファイルを開く */
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        /* fprintf(stderr, "Failed to open %s. \n", filename); */
        return NULL;
    }

    /* パーサ初期化 */
    WAVParser_Initialize(&parser, fp);

    /* ヘッダ読み取り */
    if (WAVParser_GetWAVFormat(&parser, &format) != WAV_ERROR_OK) {
        return NULL;
    }

    /* ハンドル作成 */
    wavfile = WAV_Create(&format);
    if (wavfile == NULL) {
        return NULL;
    }

    /* PCMデータ読み取り */
    if (WAVParser_GetWAVPcmData(&parser, wavfile) != WAV_ERROR_OK) {
        goto EXIT_FAILURE_WITH_DATA_RELEASE;
    }

    /* パーサ終了 */
    WAVParser_Finalize(&parser);

    /* ファイルを閉じる */
    fclose(fp);

    /* 正常終了 */
    return wavfile;

    /* ハンドルが確保したデータを全て解放して終了 */
EXIT_FAILURE_WITH_DATA_RELEASE:
    WAV_Destroy(wavfile);
    WAVParser_Finalize(&parser);
    fclose(fp);
    return NULL;
}

/* フォーマットを指定して新規にWAVファイルハンドルを作成 */
struct WAVFile* WAV_Create(const struct WAVFileFormat* format)
{
    uint32_t ch;
    struct WAVFile* wavfile;

    /* 引数チェック */
    if (format == NULL) {
        return NULL;
    }

    /* 現在はPCMフォーマット以外対応していない */
    if (format->data_format != WAV_DATA_FORMAT_PCM) {
        /* fprintf(stderr, "Unsupported wav data format. \n"); */
        return NULL;
    }

    /* ハンドル作成 */
    wavfile = (struct WAVFile *)malloc(sizeof(struct WAVFile));
    if (wavfile == NULL) {
        goto EXIT_FAILURE_WITH_DATA_RELEASE;
    }

    /* 構造体コピーによりフォーマット情報取得 */
    wavfile->format = (*format);

    /* データ領域の割り当て */
    wavfile->data = (WAVPcmData **)malloc(sizeof(WAVPcmData *) * format->num_channels);
    if (wavfile->data == NULL) {
        goto EXIT_FAILURE_WITH_DATA_RELEASE;
    }
    for (ch = 0; ch < format->num_channels; ch++) {
        wavfile->data[ch] = (WAVPcmData *)calloc(format->num_samples, sizeof(WAVPcmData));
        if (wavfile->data[ch] == NULL) {
            goto EXIT_FAILURE_WITH_DATA_RELEASE;
        }
    }

    return wavfile;

EXIT_FAILURE_WITH_DATA_RELEASE:
    WAV_Destroy(wavfile);
    return NULL;
}

/* 8bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert8bitPCMto32bitPCM(int32_t in_8bitpcm)
{
    /* 無音に相当する128を引いてから32bit整数に切り上げる */
    return (in_8bitpcm - 128) << 24;
}

/* 16bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert16bitPCMto32bitPCM(int32_t in_16bitpcm)
{
    /* そのまま16bit左シフト */
    return in_16bitpcm << 16;
}

/* 24bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert24bitPCMto32bitPCM(int32_t in_24bitpcm)
{
    /* そのまま8bit左シフト */
    return in_24bitpcm << 8;
}

/* 32bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert32bitPCMto32bitPCM(int32_t in_32bitpcm)
{
    /* 何もしない */
    return in_32bitpcm;
}

/* パーサの初期化 */
static void WAVParser_Initialize(struct WAVParser* parser, FILE* fp)
{
    parser->fp                = fp;
    memset(&parser->buffer, 0, sizeof(struct WAVBitBuffer));
    parser->buffer.byte_pos   = -1;
}

/* パーサの使用終了 */
static void WAVParser_Finalize(struct WAVParser* parser)
{
    parser->fp                = NULL;
    memset(&parser->buffer, 0, sizeof(struct WAVBitBuffer));
    parser->buffer.byte_pos   = -1;
}

/* n_bit 取得し、結果を右詰めする */
static WAVError WAVParser_GetBits(struct WAVParser* parser, uint32_t n_bits, uint64_t* bitsbuf)
{
    uint64_t tmp;
    struct WAVBitBuffer *buf = &(parser->buffer);

    /* 引数チェック */
    if (parser == NULL || bitsbuf == NULL || n_bits > 64) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* 初回読み込み */
    if (buf->byte_pos == -1) {
        if (fread(buf->bytes, sizeof(uint8_t), WAVBITBUFFER_BUFFER_SIZE, parser->fp) == 0) {
            return WAV_ERROR_IO;
        }
        buf->byte_pos   = 0;
        buf->bit_count  = 8;
    }

    /* 最上位ビットからデータを埋めていく
    * 初回ループではtmpの上位ビットにセット
    * 2回目以降は8bit単位で入力しtmpにセット */
    tmp = 0;
    while (n_bits > buf->bit_count) {
        /* 上位bitから埋めていく */
        n_bits  -= buf->bit_count;
        tmp     |= (uint64_t)WAV_GetLowerBits(buf->bit_count, buf->bytes[buf->byte_pos]) << n_bits;

        /* 1バイト読み進める */
        buf->byte_pos++;
        buf->bit_count   = 8;

        /* バッファが一杯ならば、再度読み込み */
        if (buf->byte_pos == WAVBITBUFFER_BUFFER_SIZE) {
            if (fread(buf->bytes, sizeof(uint8_t), WAVBITBUFFER_BUFFER_SIZE, parser->fp) == 0) {
                return WAV_ERROR_IO;
            }
            buf->byte_pos = 0;
        }
    }

    /* 端数ビットの処理
    * 残ったビット分をtmpの最上位ビットにセット */
    buf->bit_count -= n_bits;
    tmp            |= (uint64_t)WAV_GetLowerBits(n_bits, (uint32_t)(buf->bytes[buf->byte_pos] >> buf->bit_count));

    *bitsbuf = tmp;
    return WAV_ERROR_OK;
}

/* シーク（fseek準拠） */
static WAVError WAVParser_Seek(struct WAVParser* parser, int32_t offset, int32_t wherefrom)
{
    if (parser->buffer.byte_pos != -1) {
        /* バッファに取り込んだ分先読みしているので戻す */
        offset -= (WAVBITBUFFER_BUFFER_SIZE - (parser->buffer.byte_pos + 1));
    }
    /* 移動 */
    fseek(parser->fp, offset, wherefrom);
    /* バッファをクリア */
    parser->buffer.byte_pos = -1;

    return WAV_ERROR_OK;
}

/* WAVファイルハンドルを破棄 */
void WAV_Destroy(struct WAVFile* wavfile)
{
    uint32_t ch;

    /* NULLチェックして解放 */
#define NULLCHECK_AND_FREE(ptr) { \
    if ((ptr) != NULL) {            \
        free(ptr);                    \
        ptr = NULL;                   \
    }                               \
}

    if (wavfile != NULL) {
        for (ch = 0; ch < wavfile->format.num_channels; ch++) {
            NULLCHECK_AND_FREE(wavfile->data[ch]);
        }
        NULLCHECK_AND_FREE(wavfile->data);
        free(wavfile);
    }

#undef NULLCHECK_AND_FREE
}

/* ライタを使用してファイルフォーマットに従ったヘッダ部を出力 */
static WAVError WAVWriter_PutWAVHeader(
        struct WAVWriter* writer, const struct WAVFileFormat* format)
{
    uint32_t filesize, pcm_data_size;

    /* 引数チェック */
    if (writer == NULL || format == NULL) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* フォーマットチェック */
    /* PCM以外は対応していない */
    if (format->data_format != WAV_DATA_FORMAT_PCM) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* PCM データサイズ */
    pcm_data_size
        = format->num_samples * (format->bits_per_sample / 8) * format->num_channels;

    /* ファイルサイズ */
    /* 44は"RIFF" から ("data"のサイズ) までのフィールドのバイト数（拡張部分を一切含まない） */
    filesize = pcm_data_size + 44;

    /* ヘッダ 'R', 'I', 'F', 'F' を出力 */
    if (WAVWriter_PutBits(writer, 'R', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'I', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'F', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'F', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* ファイルサイズ-8（この要素以降のサイズ） */
    if (WAVWriter_PutLittleEndianBytes(writer, 4, filesize - 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ヘッダ 'W', 'A', 'V', 'E' を出力 */
    if (WAVWriter_PutBits(writer, 'W', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'A', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'V', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'E', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* fmtチャンクのヘッダ 'f', 'm', 't', ' ' を出力 */
    if (WAVWriter_PutBits(writer, 'f', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'm', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 't', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, ' ', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* fmtチャンクのバイト数を出力 （補足）現在は16byte決め打ち */
    if (WAVWriter_PutLittleEndianBytes(writer, 4, 16) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* フォーマットIDを出力 （補足）現在は1（リニアPCM）決め打ち */
    if (WAVWriter_PutLittleEndianBytes(writer, 2,  1) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* チャンネル数 */
    if (WAVWriter_PutLittleEndianBytes(writer, 2, format->num_channels) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* サンプリングレート */
    if (WAVWriter_PutLittleEndianBytes(writer, 4, format->sampling_rate) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* データ速度（byte/sec） */
    if (WAVWriter_PutLittleEndianBytes(writer, 4,
                format->sampling_rate * (format->bits_per_sample / 8) * format->num_channels)
            != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ブロックあたりサイズ数 */
    if (WAVWriter_PutLittleEndianBytes(writer, 2,
                (format->bits_per_sample / 8) * format->num_channels)
            != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* 量子化ビット数（サンプルあたりのビット数） */
    if (WAVWriter_PutLittleEndianBytes(writer, 2, format->bits_per_sample) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* "data" チャンクのヘッダ出力 */
    if (WAVWriter_PutBits(writer, 'd', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'a', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 't', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };
    if (WAVWriter_PutBits(writer, 'a', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; };

    /* 波形データバイト数 */
    if (WAVWriter_PutLittleEndianBytes(writer, 4, pcm_data_size) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    return WAV_ERROR_OK;
}

/* リトルエンディアンで書き出し
* 注意）dataはスワップされる可能性がある */
static size_t WAVWrite_FWriteLittleEndian(
        void *data, size_t size, size_t ndata, FILE *fp)
{
    int x = 1;
    uint8_t *buffer;
    uint32_t i;

    /* リトルエンディアン環境ではそのままfwrite */
    if ((size == 1) || (*((char *)&x) == 1)) {
        return fwrite(data, size, ndata, fp);
    }

    /* ビッグエンディアン環境では並び替えてから書き込む */
    buffer = (uint8_t *)data;

    switch (size) {
    case 2:
        for (i = 0; i < ndata; i++) {
            uint8_t a = buffer[2 * i];
            buffer[2 * i + 0] = buffer[2 * i + 1];
            buffer[2 * i + 1] = a;
        }
        break;
    case 3:
        for (i = 0; i < ndata; i++) {
            uint8_t a = buffer[3 * i];
            buffer[3 * i + 0] = buffer[3 * i + 2];
            buffer[3 * i + 2] = a;
        }
        break;
    case 4:
        for (i = 0; i < ndata; i++) {
            uint8_t a = buffer[4 * i];
            uint8_t b = buffer[4 * i + 1];
            buffer[4 * i + 0] = buffer[4 * i + 3];
            buffer[4 * i + 1] = buffer[4 * i + 2];
            buffer[4 * i + 2] = b;
            buffer[4 * i + 3] = a;
        }
        break;
    default:
        return 0;
    }

    return fwrite(data, size, ndata, fp);
}

/* ライタを使用してPCMデータ出力 */
static WAVError WAVWriter_PutWAVPcmData(
        struct WAVWriter* writer, const struct WAVFile* wavfile)
{
    uint32_t ch, smpl, progress;

    /* バッファは空に */
    WAVWriter_Flush(writer);

    /* チャンネルインターリーブしながら書き出し */
    switch (wavfile->format.bits_per_sample) {
    case 8:
        {
            uint8_t *buffer;
            const uint32_t num_output_smpls_per_buffer = WAVBITBUFFER_BUFFER_SIZE / (sizeof(uint8_t) * wavfile->format.num_channels);
            progress = 0;
            while (progress < wavfile->format.num_samples) {
                const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
                const uint32_t num_output_smpls = num_process_smpls * wavfile->format.num_channels;
                buffer = (uint8_t *)writer->buffer.bytes;
                for (smpl = 0; smpl < num_process_smpls; smpl++) {
                    for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                        (*buffer++) = (uint8_t)(((WAVFile_PCM(wavfile, progress + smpl, ch) >> 24) + 128) & 0xFF);
                    }
                }
                if (WAVWrite_FWriteLittleEndian(writer->buffer.bytes,
                            sizeof(uint8_t), num_output_smpls, writer->fp) < num_output_smpls) {
                    return WAV_ERROR_IO;
                }
                progress += num_process_smpls;
            }
        }
        break;
    case 16:
        {
            int16_t *buffer;
            const uint32_t num_output_smpls_per_buffer = WAVBITBUFFER_BUFFER_SIZE / (sizeof(int16_t) * wavfile->format.num_channels);
            progress = 0;
            while (progress < wavfile->format.num_samples) {
                const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
                const uint32_t num_output_smpls = num_process_smpls * wavfile->format.num_channels;
                buffer = (int16_t *)writer->buffer.bytes;
                for (smpl = 0; smpl < num_process_smpls; smpl++) {
                    for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                        (*buffer++) = (int16_t)((WAVFile_PCM(wavfile, progress + smpl, ch) >> 16) & 0xFFFF);
                    }
                }
                if (WAVWrite_FWriteLittleEndian(writer->buffer.bytes,
                            sizeof(int16_t), num_output_smpls, writer->fp) < num_output_smpls) {
                    return WAV_ERROR_IO;
                }
                progress += num_process_smpls;
            }
        }
        break;
    case 24:
        {
            uint8_t *buffer;
            const size_t int24_size = 3 * sizeof(uint8_t);
            const uint32_t num_output_smpls_per_buffer = WAVBITBUFFER_BUFFER_SIZE / (int24_size * wavfile->format.num_channels);
            progress = 0;
            while (progress < wavfile->format.num_samples) {
                const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
                const uint32_t num_output_smpls = num_process_smpls * wavfile->format.num_channels;
                const size_t output_size = num_output_smpls * int24_size;
                buffer = (uint8_t *)writer->buffer.bytes;
                for (smpl = 0; smpl < num_process_smpls; smpl++) {
                    for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                        int32_t pcm = WAVFile_PCM(wavfile, progress + smpl, ch);
                        (*buffer++) = (uint8_t)((pcm >>  8) & 0xFF);
                        (*buffer++) = (uint8_t)((pcm >> 16) & 0xFF);
                        (*buffer++) = (uint8_t)((pcm >> 24) & 0xFF);
                    }
                }
                if (WAVWrite_FWriteLittleEndian(writer->buffer.bytes,
                            sizeof(uint8_t), output_size, writer->fp) < output_size) {
                    return WAV_ERROR_IO;
                }
                progress += num_process_smpls;
            }
        }
        break;
    case 32:
        {
            int32_t *buffer;
            const uint32_t num_output_smpls_per_buffer = WAVBITBUFFER_BUFFER_SIZE / (sizeof(int32_t) * wavfile->format.num_channels);
            progress = 0;
            while (progress < wavfile->format.num_samples) {
                const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
                const uint32_t num_output_smpls = num_process_smpls * wavfile->format.num_channels;
                buffer = (int32_t *)writer->buffer.bytes;
                for (smpl = 0; smpl < num_process_smpls; smpl++) {
                    for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                        (*buffer++) = WAVFile_PCM(wavfile, progress + smpl, ch);
                    }
                }
                if (WAVWrite_FWriteLittleEndian(writer->buffer.bytes,
                            sizeof(int32_t), num_output_smpls, writer->fp) < num_output_smpls) {
                    return WAV_ERROR_IO;
                }
                progress += num_process_smpls;
            }
        }
        break;
    default:
        /* fprintf(stderr, "Unsupported bits per smpl format(=%d). \n", wavfile->format.bits_per_smpl); */
        return WAV_ERROR_INVALID_FORMAT;
    }


    return WAV_ERROR_OK;
}

/* ファイル書き出し */
WAVApiResult WAV_WriteToFile(
        const char* filename, const struct WAVFile* wavfile)
{
    struct WAVWriter  writer;
    FILE*             fp;

    /* 引数チェック */
    if (filename == NULL || wavfile == NULL) {
        return WAV_APIRESULT_INVALID_PARAMETER;
    }

    /* wavファイルを開く */
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        /* fprintf(stderr, "Failed to open %s. \n", filename); */
        return WAV_APIRESULT_NG;
    }

    /* ライタ初期化 */
    WAVWriter_Initialize(&writer, fp);

    /* ヘッダ書き出し */
    if (WAVWriter_PutWAVHeader(&writer, &wavfile->format) != WAV_ERROR_OK) {
        return WAV_APIRESULT_NG;
    }

    /* データ書き出し */
    if (WAVWriter_PutWAVPcmData(&writer, wavfile) != WAV_ERROR_OK) {
        return WAV_APIRESULT_NG;
    }

    /* ライタ終了 */
    WAVWriter_Finalize(&writer);

    /* ファイルを閉じる */
    fclose(fp);

    /* 正常終了 */
    return WAV_APIRESULT_OK;
}

/* ライタの初期化 */
static void WAVWriter_Initialize(struct WAVWriter* writer, FILE* fp)
{
    writer->fp                = fp;
    writer->bit_count         = 8;
    writer->bit_buffer        = 0;
    memset(&writer->buffer, 0, sizeof(struct WAVBitBuffer));
    writer->buffer.byte_pos   = 0;
}

/* ライタの終了 */
static void WAVWriter_Finalize(struct WAVWriter* writer)
{
    /* バッファに余っているデータを書き出し */
    WAVWriter_Flush(writer);

    /* メンバをクリア */
    writer->fp              = NULL;
    writer->bit_count       = 8;
    writer->bit_buffer      = 0;
    memset(&writer->buffer, 0, sizeof(struct WAVBitBuffer));
    writer->buffer.byte_pos = 0;
}

/* valの下位n_bitを書き込む（ビッグエンディアンで） */
static WAVError WAVWriter_PutBits(struct WAVWriter* writer, uint64_t val, uint32_t n_bits)
{
    /* 無効な引数 */
    if (writer == NULL) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* valの上位ビットから順次出力
    * 初回ループでは端数（出力に必要なビット数）分を埋め出力
    * 2回目以降は8bit単位で出力 */
    while (n_bits >= writer->bit_count) {
        n_bits -= writer->bit_count;
        writer->bit_buffer |= (uint8_t)WAV_GetLowerBits(writer->bit_count, val >> n_bits);

        /* バッファに追記 */
        writer->buffer.bytes[writer->buffer.byte_pos++] = (uint8_t)(writer->bit_buffer & 0xFF);

        /* バッファが一杯になったら書き出し */
        if (writer->buffer.byte_pos == WAVBITBUFFER_BUFFER_SIZE) {
            if (fwrite(writer->buffer.bytes,
                        sizeof(uint8_t), WAVBITBUFFER_BUFFER_SIZE,
                        writer->fp) < WAVBITBUFFER_BUFFER_SIZE) {
                return WAV_ERROR_IO;
            }
            /* 書き込み位置をリセット */
            writer->buffer.byte_pos = 0;
        }

        writer->bit_buffer  = 0;
        writer->bit_count   = 8;
    }

    /* 端数ビットの処理:
    * 残った分をバッファの上位ビットにセット */
    writer->bit_count -= n_bits;
    writer->bit_buffer |= (uint8_t)(WAV_GetLowerBits(n_bits, (uint32_t)val) << writer->bit_count);

    return WAV_ERROR_OK;
}

/* リトルエンディアンでビットパターンを出力 */
static WAVError WAVWriter_PutLittleEndianBytes(
        struct WAVWriter* writer, uint32_t nbytes, uint64_t data)
{
    uint64_t out;
    uint32_t i_byte;

    /* リトルエンディアンに並び替え */
    out = 0;
    for (i_byte = 0; i_byte < nbytes; i_byte++) {
        out |= ((data >> (8 * (nbytes - i_byte - 1))) & 0xFFUL) << (8 * i_byte);
    }

    /* 出力 */
    if (WAVWriter_PutBits(writer, out, (uint8_t)(nbytes * 8)) != WAV_ERROR_OK) {
        return WAV_ERROR_IO;
    }

    return WAV_ERROR_OK;
}

/* バッファにたまったビットをクリア */
static WAVError WAVWriter_Flush(struct WAVWriter* writer)
{
    /* 引数チェック */
    if (writer == NULL) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* 余ったビットを強制出力 */
    if (writer->bit_count != 8) {
        if (WAVWriter_PutBits(writer, 0, (uint8_t)writer->bit_count) != WAV_ERROR_OK) {
            return WAV_ERROR_IO;
        }
        writer->bit_buffer = 0;
        writer->bit_count  = 8;
    }

    /* バッファに残っているデータをフラッシュ */
    if (fwrite(writer->buffer.bytes,
                sizeof(uint8_t), (uint32_t)writer->buffer.byte_pos,
                writer->fp) < (size_t)writer->buffer.byte_pos) {
        return WAV_ERROR_IO;
    }
    /* バッファ残量は0に */
    writer->buffer.byte_pos = 0;

    return WAV_ERROR_OK;
}

/* リトルエンディアンでビットパターンを取得 */
static WAVError WAVParser_GetLittleEndianBytes(
        struct WAVParser* parser, uint32_t nbytes, uint64_t* bitsbuf)
{
    uint64_t tmp, ret;
    uint32_t i_byte;

    /* ビッグエンディアンで取得 */
    if (WAVParser_GetBits(parser, nbytes * 8, &tmp) != WAV_ERROR_OK) {
        return WAV_ERROR_IO;
    }

    /* リトルエンディアンに並び替え */
    ret = 0;
    for (i_byte = 0; i_byte < nbytes; i_byte++) {
        ret |= ((tmp >> (8 * (nbytes - i_byte - 1))) & 0xFFUL) << (8 * i_byte);
    }
    *bitsbuf = ret;

    return WAV_ERROR_OK;
}

/* パーサを使用して文字列取得 */
static WAVError WAVParser_GetString(
        struct WAVParser* parser, char* string_buffer, uint32_t string_length)
{
    uint32_t i_byte;
    uint64_t bitsbuf;

    assert(parser != NULL && string_buffer != NULL);

    /* 文字列取得 */
    for (i_byte = 0; i_byte < string_length; i_byte++) {
        /* 1文字取得 */
        if (WAVParser_GetBits(parser, 8, &bitsbuf) != WAV_ERROR_OK) {
            return WAV_ERROR_IO;
        }
        string_buffer[i_byte] = (char)bitsbuf;
    }

    return WAV_ERROR_OK;
}

/* パーサを使用して文字列取得/一致チェック */
static WAVError WAVParser_CheckSignatureString(
        struct WAVParser* parser, const char* signature, uint32_t signature_length)
{
    uint32_t i_byte;
    uint64_t bitsbuf;

    assert(parser != NULL && signature != NULL);

    /* 文字列取得/検査 */
    for (i_byte = 0; i_byte < signature_length; i_byte++) {
        /* 1文字取得 */
        if (WAVParser_GetBits(parser, 8, &bitsbuf) != WAV_ERROR_OK) {
            return WAV_ERROR_IO;
        }
        /* シグネチャ検査 */
        if (signature[i_byte] != (char)bitsbuf) {
            /* fprintf(stderr, "Failed to check %s header signature. \n", signature); */
            return WAV_ERROR_INVALID_FORMAT;
        }
    }

    return WAV_ERROR_OK;
}
