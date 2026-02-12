#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* パーサの読み込みバッファサイズ */
#define WAVBITBUFFER_BUFFER_SIZE (32 * 1024)

/* 下位n_bitsを取得 */
/* 補足）((1 << n_bits) - 1)は下位の数値だけ取り出すマスクになる */
#define WAV_GetLowerBits(n_bits, val) ((val) & (uint32_t)((1 << (n_bits)) - 1))

/* a,bの内の小さい値を取得 */
#define WAV_Min(a, b) (((a) < (b)) ? (a) : (b))

/* 内部エラー型 */
typedef enum {
    WAV_ERROR_OK = 0,             /* OK */
    WAV_ERROR_NG,                 /* 分類不能な失敗 */
    WAV_ERROR_IO,                 /* 入出力エラー */
    WAV_ERROR_INVALID_PARAMETER,  /* 不正な引数 */
    WAV_ERROR_INVALID_FORMAT      /* 不正なフォーマット */
} WAVError;

/* ファイル種別 */
typedef enum {
    WAV_FILETYPE_WAV = 0, /* wavファイル */
    WAV_FILETYPE_AIFF, /* aiffファイル */
    WAV_FILETYPE_INVALID, /* 無効なファイル */
} WAVFileType;

/* ビットバッファ */
struct WAVBitBuffer {
    uint8_t bytes[WAVBITBUFFER_BUFFER_SIZE]; /* ビットバッファ */
    uint32_t bit_count; /* ビット入力カウント */
    int32_t byte_pos; /* バイト列読み込み位置 */
};

/* パーサ */
struct WAVParser {
    FILE *fp;       /* 読み込みファイルポインタ */
    struct WAVBitBuffer buffer;   /* ビットバッファ */
};

/* ライタ */
struct WAVWriter {
    FILE *fp;                 /* 書き込みファイルポインタ */
    uint32_t bit_buffer;         /* 出力途中のビット */
    uint32_t bit_count;          /* 出力カウント     */
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
/* ファイル種別の判定 */
static WAVFileType WAVParser_IdentifyFileType(struct WAVParser *parser);
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
/* ビックエンディアンでビットパターンを出力 */
static WAVError WAVWriter_PutBigEndianBytes(
    struct WAVWriter *writer, uint32_t nbytes, uint64_t data);

/* WAVファイルのヘッダ部を出力 */
static WAVError WAVWriter_PutWAVHeader(
        struct WAVWriter* writer, const struct WAVFormat* format);
/* WAVファイルのPCMデータ出力 */
static WAVError WAVWriter_PutWAVPcmData(
        struct WAVWriter* writer, const struct WAVFile* wavfile);
/* AIFFファイルのヘッダ部を出力 */
static WAVError WAVWriter_PutAIFFHeader(
    struct WAVWriter *writer, const struct WAVFormat *format);
/* AIFFファイルのPCMデータ出力 */
static WAVError WAVWriter_PutAIFFPcmData(
    struct WAVWriter *writer, const struct WAVFile *wavfile);

/* リトルエンディアンでビットパターンを取得 */
static WAVError WAVParser_GetLittleEndianBytes(
        struct WAVParser* parser, uint32_t nbytes, uint64_t* bitsbuf);
/* ビックエンディアンでビットパターンを取得 */
static WAVError WAVParser_GetBigEndianBytes(
    struct WAVParser *parser, uint32_t nbytes, uint64_t *bitsbuf);
/* 文字列取得 */
static WAVError WAVParser_GetString(
        struct WAVParser* parser, char* string_buffer, uint32_t string_length);
/* 文字列一致チェック */
static WAVError WAVParser_CheckSignatureString(
        struct WAVParser* parser, const char* signature, uint32_t signature_length);
/* WAVファイルフォーマットを読み取り */
static WAVError WAVParser_GetWAVFormat(
        struct WAVParser* parser, struct WAVFormat* format);
/* AIFFファイルフォーマットを読み取り */
static WAVError WAVParser_GetAIFFFormat(
    struct WAVParser *parser, struct WAVFormat *format);
/* WAVファイルのPCMデータを読み取り */
static WAVError WAVParser_GetWAVPcmData(
        struct WAVParser* parser, struct WAVFile* wavfile);
/* AIFFファイルのPCMデータを読み取り */
static WAVError WAVParser_GetAIFFPcmData(
    struct WAVParser *parser, struct WAVFile *wavfile);

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

/* AIFFのサンプリングレートを取得 */
static WAVError WAV_ParseAIFFSamplingRate(const uint8_t *data, size_t data_size, double *sampling_rate);
/* AIFFのサンプリングレートを書き出し */
static WAVError WAV_MakeAIFFSamplingRate(double sampling_rate, uint8_t *data, size_t data_size);

/* WAVフォーマットの読み取り */
static WAVError WAVParser_ParseWAVFormat(struct WAVParser *parser, struct WAVFormat *format)
{
    uint64_t bitsbuf;
    int32_t fmt_chunk_size;
    struct WAVFormat tmp_format;

    assert(parser != NULL);
    assert(format != NULL);

    /* 仮の構造体にコピー */
    tmp_format = (*format);

    /* fmtチャンクのバイト数を取得 */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    fmt_chunk_size = (int32_t)bitsbuf;

    /* サイズを元にファイルフォーマットを判定 */
    switch (fmt_chunk_size) {
    case 16: tmp_format.file_format = WAV_FILEFORMAT_PCMWAVEFORMAT; break;
    case 40: tmp_format.file_format = WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE; break;
    default:
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* フォーマットIDをチェック
    * 補足）1（リニアPCM）以外対応していない */
    if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (((tmp_format.file_format == WAV_FILEFORMAT_PCMWAVEFORMAT) && (bitsbuf != 1))
        || ((tmp_format.file_format == WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE) && (bitsbuf != 0xFFFE))){
        /* fprintf(stderr, "Unsupported format: fmt chunk format ID \n"); */
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* チャンネル数 */
    if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    tmp_format.num_channels = (uint32_t)bitsbuf;

    /* サンプリングレート */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    tmp_format.sampling_rate = (uint32_t)bitsbuf;

    /* データ速度（byte/sec）は読み飛ばし */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ブロックあたりサイズ数は読み飛ばし */
    if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* 量子化ビット数（サンプルあたりのビット数） */
    if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    tmp_format.bits_per_sample = (uint32_t)bitsbuf;

    /* WAVFORMATEX以上の場合 */
    if (fmt_chunk_size >= 18) {
        /* 追加のサイズ情報 */
        if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* WAVEFORMATEXTENSIBLEならば22のはず */
        if ((tmp_format.file_format == WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE) && (bitsbuf != 22)) {
            return WAV_ERROR_INVALID_FORMAT;
        }
    }

    /* WAVEFORMATEXTENSIBLEの拡張部分読み取り */
    if (fmt_chunk_size == 40) {
        char tmp_guid[16] = { 0, };
        /* サンプル形式 */
        if (WAVParser_GetLittleEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        tmp_format.u.wav_extensible.sample_information = (uint16_t)bitsbuf;
        /* チャンネルマスク */
        if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        tmp_format.u.wav_extensible.channel_mask = (uint32_t)bitsbuf;
        /* GUID */
        if (WAVParser_GetString(parser, tmp_guid, 16) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        memcpy(tmp_format.u.wav_extensible.guid, tmp_guid, sizeof(tmp_format.u.wav_extensible.guid));
    }

    /* 成功終了 */
    (*format) = tmp_format;
    return WAV_ERROR_OK;
}

/* WAVファイルフォーマットを読み取り */
static WAVError WAVParser_GetWAVFormat(
        struct WAVParser* parser, struct WAVFormat* format)
{
    uint64_t bitsbuf;
    struct WAVFormat tmp_format = { WAV_FILEFORMAT_INVALID, 0, };

    /* 引数チェック */
    if ((parser == NULL) || (format == NULL)) {
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

    /* フォーマットの解釈 */
    if (WAVParser_ParseWAVFormat(parser, &tmp_format) != WAV_ERROR_OK) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* チャンク読み取り */
    while (1) {
        char string_buf[5] = { 0, };
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
            fprintf(stderr, "WARNING: skiping chunk:%s size:%d \n", string_buf, (int32_t)bitsbuf);
            WAVParser_Seek(parser, (int32_t)bitsbuf, SEEK_CUR);
        }
    }

    /* サンプル数: 波形データバイト数から算出 */
    if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    tmp_format.num_samples = (uint32_t)bitsbuf;
    assert(tmp_format.num_samples % ((tmp_format.bits_per_sample / 8) * tmp_format.num_channels) == 0);
    tmp_format.num_samples /= ((tmp_format.bits_per_sample / 8) * tmp_format.num_channels);

    /* 構造体コピー */
    (*format) = tmp_format;

    return WAV_ERROR_OK;
}

/* AIFFファイルフォーマットを読み取り */
static WAVError WAVParser_GetAIFFFormat(
    struct WAVParser *parser, struct WAVFormat *format)
{
#define MAX_NUM_MARKERS 10
    int32_t comm_chunk_exist, loop_related_chunk_count, num_markers;
    uint64_t bitsbuf;
    struct WAVFormat tmp_format = { WAV_FILEFORMAT_INVALID, 0, };
    struct Marker {
        uint16_t id;
        uint32_t position;
    } markers[MAX_NUM_MARKERS] = { 0, };
    struct Inst {
        uint8_t key;
        int16_t gain;
        uint16_t begin_loop_id;
        uint16_t end_loop_id;
    } inst = { 0, };

    /* 引数チェック */
    if ((parser == NULL) || (format == NULL)) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* ヘッダ 'F', 'O', 'R', 'M' をチェック */
    if (WAVParser_CheckSignatureString(parser, "FORM", 4) != WAV_ERROR_OK) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* ファイルサイズ-8（読み飛ばし） */
    if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ヘッダ 'A', 'I', 'F', 'F' をチェック */
    if (WAVParser_CheckSignatureString(parser, "AIFF", 4) != WAV_ERROR_OK) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* COMMチャンク探索・波形情報取得 */
    comm_chunk_exist = loop_related_chunk_count = 0;
    while (1) {
        char string_buf[5] = { 0, };
        /* チャンク文字列取得 */
        if (WAVParser_GetString(parser, string_buf, 4) != WAV_ERROR_OK) {
            /* COMMチャンクが読めていれば成功 */
            if (comm_chunk_exist == 1) {
                break;
            }
            return WAV_ERROR_IO;
        }
        if (strncmp(string_buf, "COMM", 4) == 0) {
            /* COMMチャンク */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            if (bitsbuf != 18) {
                return WAV_ERROR_INVALID_FORMAT;
            }
            /* チャンネル数 */
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            tmp_format.num_channels = (uint32_t)bitsbuf;
            /* サンプル数 */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            tmp_format.num_samples = (uint32_t)bitsbuf;
            /* サンプルあたりビット数 */
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            tmp_format.bits_per_sample = (uint32_t)bitsbuf;
            /* サンプリングレート */
            {
                int32_t i;
                uint8_t buf[10] = { 0, };
                double tmp;
                for (i = 0; i < 10; i++) {
                    if (WAVParser_GetBits(parser, 8, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
                    buf[i] = (uint8_t)bitsbuf;
                }
                if (WAV_ParseAIFFSamplingRate(buf, sizeof(buf), &tmp) != WAV_ERROR_OK) {
                    return WAV_ERROR_INVALID_FORMAT;
                }
                /* 注意：小数点以下は切り捨て */
                tmp_format.sampling_rate = (uint32_t)tmp;
            }
            comm_chunk_exist = 1;
        } else if (strncmp(string_buf, "MARK", 4) == 0) {
            /* MARKチャンク */
            int32_t i;
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* マーカー数 */
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            num_markers = (int32_t)bitsbuf;
            if (num_markers > MAX_NUM_MARKERS) {
                return WAV_ERROR_INVALID_FORMAT;
            }
            for (i = 0; i < num_markers; i++) {
                /* ID */
                if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
                markers[i].id = (uint16_t)bitsbuf;
                /* サンプル位置 */
                if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
                markers[i].position = (uint32_t)bitsbuf;
                /* マーカー名は読み捨てる */
                if (WAVParser_GetBigEndianBytes(parser, 1, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
                WAVParser_Seek(parser, (int32_t)bitsbuf + 1, SEEK_CUR);
            }
            loop_related_chunk_count++;
        } else if (strncmp(string_buf, "INST", 4) == 0) {
            /* INSTチャンク */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            if (bitsbuf != 20) {
                return WAV_ERROR_INVALID_FORMAT;
            }
            /* キー（ノート）番号 */
            if (WAVParser_GetBigEndianBytes(parser, 1, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            inst.key = (uint8_t)bitsbuf;
            /* デチューン（読み飛ばし） */
            if (WAVParser_GetBigEndianBytes(parser, 1, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* 最小ノート番号（読み飛ばし） */
            if (WAVParser_GetBigEndianBytes(parser, 1, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* 最大ノート番号（読み飛ばし） */
            if (WAVParser_GetBigEndianBytes(parser, 1, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* 最小ベロシティ（読み飛ばし） */
            if (WAVParser_GetBigEndianBytes(parser, 1, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* 最大ベロシティ（読み飛ばし） */
            if (WAVParser_GetBigEndianBytes(parser, 1, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* ゲイン */
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            inst.gain = (int16_t)(bitsbuf & 0xFFFF);
            /* サステインループ */
            /* ループ再生モード（読み飛ばし） */
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* ループ開始マーカーID */
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            inst.begin_loop_id = (uint16_t)bitsbuf;
            /* ループ終点マーカーID */
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            inst.end_loop_id = (uint16_t)bitsbuf;
            /* リリースループ（読み飛ばし） */
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            if (WAVParser_GetBigEndianBytes(parser, 2, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            loop_related_chunk_count++;
        } else if (strncmp(string_buf, "SSND", 4) == 0) {
            /* サウンドデータチャンクの読み飛ばし */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) {
                return WAV_ERROR_IO;
            }
            WAVParser_Seek(parser, (int32_t)bitsbuf, SEEK_CUR);
        } else {
            /* 他のチャンクはサイズだけ取得してシークにより読み飛ばす */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) {
                return WAV_ERROR_IO;
            }
            /* 奇数サイズのチャンクは偶数に */
            if (bitsbuf & 1) {
                bitsbuf += 1;
            }
            fprintf(stderr, "WARNING: skiping chunk:%s size:%d \n", string_buf, (int32_t)bitsbuf);
            WAVParser_Seek(parser, (int32_t)bitsbuf, SEEK_CUR);
        }
    }

    /* COMMチャンクがなかったりループに関連するチャンク数が不正な場合はエラー */
    if ((comm_chunk_exist != 1) || (loop_related_chunk_count == 1) || (loop_related_chunk_count > 2)) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* ループに関連する情報取得 */
    if (loop_related_chunk_count == 2) {
        int32_t i, loop_id_count = 0;

        /* ループマーカーを探す */
        for (i = 0; i < num_markers; i++) {
            if (markers[i].id == inst.begin_loop_id) {
                tmp_format.u.aiff_instrument.loop_begin = markers[i].position;
                loop_id_count++;
            } else if (markers[i].id == inst.end_loop_id) {
                tmp_format.u.aiff_instrument.loop_end = markers[i].position;
                loop_id_count++;
            }
        }

        /* キーとゲインを取得 */
        tmp_format.u.aiff_instrument.key = inst.key;
        tmp_format.u.aiff_instrument.gain = inst.gain;

        /* ループマーカー数が異常 */
        if (loop_id_count != 2) {
            return WAV_ERROR_INVALID_FORMAT;
        }
    }

    /* 成功終了 */
    tmp_format.file_format = WAV_FILEFORMAT_AIFF;
    (*format) = tmp_format;

    return WAV_ERROR_OK;
}

/* WAVファイルのPCMデータを読み取り */
static WAVError WAVParser_GetWAVPcmData(
    struct WAVParser *parser, struct WAVFile *wavfile)
{
    uint32_t ch, sample, bytes_per_sample;
    uint64_t bitsbuf;
    int32_t(*convert_to_sint32_func)(int32_t);

    /* 引数チェック */
    if ((parser == NULL) || (wavfile == NULL)) {
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

    /* "data"チャンクを見つけるまで読み飛ばし */
    while (1) {
        char string_buf[4];
        /* チャンク文字列取得 */
        if (WAVParser_GetString(parser, string_buf, 4) != WAV_ERROR_OK) {
            return WAV_ERROR_IO;
        }
        if (strncmp(string_buf, "data", 4) == 0) {
            /* データチャンクを見つけたらサイズを空読みして終わり */
            if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            break;
        } else {
            /* 他のチャンクはサイズだけ取得してシークにより読み飛ばす */
            if (WAVParser_GetLittleEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) {
                return WAV_ERROR_IO;
            }
            WAVParser_Seek(parser, (int32_t)bitsbuf, SEEK_CUR);
        }
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

/* AIFFファイルのPCMデータを読み取り */
static WAVError WAVParser_GetAIFFPcmData(
        struct WAVParser* parser, struct WAVFile* wavfile)
{
    uint32_t ch, sample, bytes_per_sample;
    uint64_t bitsbuf;
    int32_t (*convert_to_sint32_func)(int32_t);

    /* 引数チェック */
    if ((parser == NULL) || (wavfile == NULL)) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* ヘッダ 'F', 'O', 'R', 'M' をチェック */
    if (WAVParser_CheckSignatureString(parser, "FORM", 4) != WAV_ERROR_OK) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* ファイルサイズ-8（読み飛ばし） */
    if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ヘッダ 'A', 'I', 'F', 'F' をチェック */
    if (WAVParser_CheckSignatureString(parser, "AIFF", 4) != WAV_ERROR_OK) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* "SSND"チャンクを見つけるまで読み飛ばし */
    while (1) {
        char string_buf[4];
        /* チャンク文字列取得 */
        if (WAVParser_GetString(parser, string_buf, 4) != WAV_ERROR_OK) {
            return WAV_ERROR_IO;
        }
        if (strncmp(string_buf, "SSND", 4) == 0) {
            int32_t offset_size;
            /* サイズを空読み */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* オフセットバイト */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            offset_size = (int32_t)bitsbuf;
            /* ブロックサイズを空読み */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
            /* コメントを読み飛ばし */
            WAVParser_Seek(parser, offset_size, SEEK_CUR);
            break;
        } else {
            /* 他のチャンクはサイズだけ取得してシークにより読み飛ばす */
            if (WAVParser_GetBigEndianBytes(parser, 4, &bitsbuf) != WAV_ERROR_OK) {
                return WAV_ERROR_IO;
            }
            /* 奇数サイズのチャンクは偶数に */
            if (bitsbuf & 1) {
                bitsbuf += 1;
            }
            WAVParser_Seek(parser, (int32_t)bitsbuf, SEEK_CUR);
        }
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
            if (WAVParser_GetBigEndianBytes(parser, bytes_per_sample, &bitsbuf) != WAV_ERROR_OK) {
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
        const char* filename, struct WAVFormat* format)
{
    struct WAVParser parser;
    WAVFileType file_type;
    FILE* fp;

    /* 引数チェック */
    if ((filename == NULL) || (format == NULL)) {
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

    /* ファイル種別の判定 */
    if ((file_type = WAVParser_IdentifyFileType(&parser)) == WAV_FILETYPE_INVALID) {
        return WAV_APIRESULT_NG;
    }

    /* 参照位置を先頭に戻す */
    WAVParser_Seek(&parser, 0, SEEK_SET);

    /* ヘッダ読み取り */
    switch (file_type) {
    case WAV_FILETYPE_WAV:
        if (WAVParser_GetWAVFormat(&parser, format) != WAV_ERROR_OK) {
            return WAV_APIRESULT_INVALID_FORMAT;
        }
        break;
    case WAV_FILETYPE_AIFF:
        if (WAVParser_GetAIFFFormat(&parser, format) != WAV_ERROR_OK) {
            return WAV_APIRESULT_INVALID_FORMAT;
        }
        break;
    default:
        return WAV_APIRESULT_INVALID_FORMAT;
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
    struct WAVParser parser;
    FILE* fp;
    struct WAVFile* wavfile;
    struct WAVFormat format;
    WAVFileType file_type;

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

    /* ファイル種別の判定 */
    if ((file_type = WAVParser_IdentifyFileType(&parser)) == WAV_FILETYPE_INVALID) {
        return NULL;
    }

    /* 参照位置を先頭に戻す */
    WAVParser_Seek(&parser, 0, SEEK_SET);

    /* ヘッダ読み取り */
    switch (file_type) {
    case WAV_FILETYPE_WAV:
        if (WAVParser_GetWAVFormat(&parser, &format) != WAV_ERROR_OK) {
            return NULL;
        }
        break;
    case WAV_FILETYPE_AIFF:
        if (WAVParser_GetAIFFFormat(&parser, &format) != WAV_ERROR_OK) {
            return NULL;
        }
        break;
    default:
        return NULL;
    }

    /* ハンドル作成 */
    wavfile = WAV_Create(&format);
    if (wavfile == NULL) {
        return NULL;
    }

    /* 参照位置を先頭に戻す */
    WAVParser_Seek(&parser, 0, SEEK_SET);

    /* PCMデータ読み取り */
    switch (file_type) {
    case WAV_FILETYPE_WAV:
        if (WAVParser_GetWAVPcmData(&parser, wavfile) != WAV_ERROR_OK) {
            goto EXIT_FAILURE_WITH_DATA_RELEASE;
        }
        break;
    case WAV_FILETYPE_AIFF:
        if (WAVParser_GetAIFFPcmData(&parser, wavfile) != WAV_ERROR_OK) {
            goto EXIT_FAILURE_WITH_DATA_RELEASE;
        }
        break;
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
struct WAVFile* WAV_Create(const struct WAVFormat* format)
{
    uint32_t ch;
    struct WAVFile* wavfile;

    /* 引数チェック */
    if (format == NULL) {
        return NULL;
    }

    /* 異常なフォーマットのハンドルを作らせない */
    if ((format->file_format != WAV_FILEFORMAT_PCMWAVEFORMAT)
        && (format->file_format != WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE)
        && (format->file_format != WAV_FILEFORMAT_AIFF)) {
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
    /* 無音に相当する128を引く */
    return (in_8bitpcm - 128);
}

/* 16bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert16bitPCMto32bitPCM(int32_t in_16bitpcm)
{
    /* 一旦32bit幅にしてから算術右シフトで符号をつける */
    return (in_16bitpcm << 16) >> 16;
}

/* 24bitPCM形式を32bit形式に変換 */
static int32_t WAV_Convert24bitPCMto32bitPCM(int32_t in_24bitpcm)
{
    /* 一旦32bit幅にしてから算術右シフトで符号をつける */
    return (in_24bitpcm << 8) >> 8;
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
    parser->fp = fp;
    memset(&parser->buffer, 0, sizeof(struct WAVBitBuffer));
    parser->buffer.byte_pos = -1;
}

/* パーサの使用終了 */
static void WAVParser_Finalize(struct WAVParser* parser)
{
    parser->fp = NULL;
    memset(&parser->buffer, 0, sizeof(struct WAVBitBuffer));
    parser->buffer.byte_pos = -1;
}

/* n_bit 取得し、結果を右詰めする */
static WAVError WAVParser_GetBits(struct WAVParser* parser, uint32_t n_bits, uint64_t* bitsbuf)
{
    uint64_t tmp;
    struct WAVBitBuffer *buf = &(parser->buffer);

    /* 引数チェック */
    if ((parser == NULL) || (bitsbuf == NULL) || (n_bits > 64)) {
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
    assert(parser != NULL);

    /* バッファに取り込んだ分先読みしているので戻す */
    if ((wherefrom == SEEK_CUR) && (parser->buffer.byte_pos != -1)) {
        offset -= (WAVBITBUFFER_BUFFER_SIZE - (parser->buffer.byte_pos + 1));
    }

    /* 移動 */
    fseek(parser->fp, offset, wherefrom);

    /* バッファをクリア */
    parser->buffer.byte_pos = -1;

    return WAV_ERROR_OK;
}

/* ファイル種別の判定 */
static WAVFileType WAVParser_IdentifyFileType(struct WAVParser *parser)
{
    assert(parser != NULL);

    /* WAVファイルだと思って判定 */
    WAVParser_Seek(parser, 0, SEEK_SET);
    do {
        uint64_t tmp;

        /* ヘッダ 'R', 'I', 'F', 'F' をチェック */
        if (WAVParser_CheckSignatureString(parser, "RIFF", 4) != WAV_ERROR_OK) {
            break;
        }

        /* ファイルサイズ-8（読み飛ばし） */
        if (WAVParser_GetLittleEndianBytes(parser, 4, &tmp) != WAV_ERROR_OK) {
            break;
        }

        /* ヘッダ 'W', 'A', 'V', 'E' をチェック */
        if (WAVParser_CheckSignatureString(parser, "WAVE", 4) != WAV_ERROR_OK) {
            break;
        }

        return WAV_FILETYPE_WAV;
    } while (0);

    /* AIFFファイルだと思って判定 */
    WAVParser_Seek(parser, 0, SEEK_SET);
    do {
        uint64_t tmp;

        /* ヘッダ 'F', 'O', 'R', 'M' をチェック */
        if (WAVParser_CheckSignatureString(parser, "FORM", 4) != WAV_ERROR_OK) {
            break;
        }

        /* ファイルサイズ-8（読み飛ばし） */
        if (WAVParser_GetLittleEndianBytes(parser, 4, &tmp) != WAV_ERROR_OK) {
            break;
        }

        /* ヘッダ 'A', 'I', 'F', 'F' をチェック */
        if (WAVParser_CheckSignatureString(parser, "AIFF", 4) != WAV_ERROR_OK) {
            break;
        }

        return WAV_FILETYPE_AIFF;
    } while (0);

    return WAV_FILETYPE_INVALID;
}

/* WAVファイルハンドルを破棄 */
void WAV_Destroy(struct WAVFile* wavfile)
{
    uint32_t ch;

    /* NULLチェックして解放 */
#define NULLCHECK_AND_FREE(ptr) { \
    if ((ptr) != NULL) { \
        free(ptr); \
        ptr = NULL; \
    } \
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

/* WAVフォーマットの書き出し */
static WAVError WAVWriter_PutWAVFormat(
    struct WAVWriter *writer, const struct WAVFormat *format)
{
    uint32_t fmt_size;

    /* フォーマットからfmtチャンクのサイズを復元 */
    switch (format->file_format) {
    case WAV_FILEFORMAT_PCMWAVEFORMAT: fmt_size = 16; break;
    case WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE: fmt_size = 40; break;
    default:
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* fmtチャンクのバイト数 */
    if (WAVWriter_PutLittleEndianBytes(writer, 4, fmt_size) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* フォーマットID */
    {
        const uint16_t format_id = (format->file_format == WAV_FILEFORMAT_PCMWAVEFORMAT) ? 1 : 0xFFFE;
        if (WAVWriter_PutLittleEndianBytes(writer, 2, format_id) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    }

    /* チャンネル数 */
    if (WAVWriter_PutLittleEndianBytes(writer, 2, format->num_channels) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* サンプリングレート */
    if (WAVWriter_PutLittleEndianBytes(writer, 4, format->sampling_rate) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* データ速度（byte/sec） */
    if (WAVWriter_PutLittleEndianBytes(writer, 4,
        format->sampling_rate * (format->bits_per_sample / 8) * format->num_channels)
        != WAV_ERROR_OK) {
        return WAV_ERROR_IO;
    }

    /* ブロックあたりサイズ数 */
    if (WAVWriter_PutLittleEndianBytes(writer, 2,
        (format->bits_per_sample / 8) * format->num_channels)
        != WAV_ERROR_OK) {
        return WAV_ERROR_IO;
    }

    /* 量子化ビット数（サンプルあたりのビット数） */
    if (WAVWriter_PutLittleEndianBytes(writer, 2, format->bits_per_sample) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* WAVFORMATEX以上の場合 */
    if (fmt_size >= 18) {
        /* 追加のサイズ情報 */
        const uint16_t additional_size = (format->file_format == WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE) ? 22 : 0;
        if (WAVWriter_PutLittleEndianBytes(writer, 2, additional_size) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    }

    /* WAVEFORMATEXTENSIBLEの場合 */
    if (fmt_size == 40) {
        int32_t i;
        /* サンプル形式 */
        if (WAVWriter_PutLittleEndianBytes(writer, 2, format->u.wav_extensible.sample_information) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* チャンネルマスク */
        if (WAVWriter_PutLittleEndianBytes(writer, 4, format->u.wav_extensible.channel_mask) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* GUID */
        for (i = 0; i < 16; i++) {
            if (WAVWriter_PutBits(writer, format->u.wav_extensible.guid[i], 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        }
    }

    return WAV_ERROR_OK;
}

/* WAVファイルのヘッダ部を出力 */
static WAVError WAVWriter_PutWAVHeader(
        struct WAVWriter* writer, const struct WAVFormat* format)
{
    uint32_t filesize, pcm_data_size;

    /* 引数チェック */
    if (writer == NULL || format == NULL) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* PCM データサイズ */
    pcm_data_size
        = format->num_samples * (format->bits_per_sample / 8) * format->num_channels;

    /* ファイルサイズ */
    switch (format->file_format) {
    case WAV_FILEFORMAT_PCMWAVEFORMAT:
        filesize = pcm_data_size + 44;
        break;
    case WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE:
        filesize = pcm_data_size + 68;
        break;
    default:
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* ヘッダ 'R', 'I', 'F', 'F' を出力 */
    if (WAVWriter_PutBits(writer, 'R', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'I', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'F', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'F', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ファイルサイズ-8（この要素以降のサイズ） */
    if (WAVWriter_PutLittleEndianBytes(writer, 4, filesize - 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ヘッダ 'W', 'A', 'V', 'E' を出力 */
    if (WAVWriter_PutBits(writer, 'W', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'A', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'V', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'E', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* fmtチャンクのヘッダ 'f', 'm', 't', ' ' を出力 */
    if (WAVWriter_PutBits(writer, 'f', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'm', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 't', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, ' ', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* フォーマットの書き出し */
    if (WAVWriter_PutWAVFormat(writer, format) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* dataチャンクのヘッダ出力 */
    if (WAVWriter_PutBits(writer, 'd', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'a', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 't', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'a', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* 波形データバイト数 */
    if (WAVWriter_PutLittleEndianBytes(writer, 4, pcm_data_size) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    return WAV_ERROR_OK;
}

/* リトルエンディアンで書き出し
* 注意）dataはスワップされる可能性がある */
static size_t WAVWriter_FWriteLittleEndian(
        void *data, size_t size, size_t ndata, FILE *fp)
{
    const int x = 1;
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

/* WAVファイルのPCMデータ出力 */
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
                        (*buffer++) = (uint8_t)((WAVFile_PCM(wavfile, progress + smpl, ch) + 128) & 0xFF);
                    }
                }
                if (WAVWriter_FWriteLittleEndian(writer->buffer.bytes,
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
            const uint32_t num_output_smpls_per_buffer = (uint32_t)(WAVBITBUFFER_BUFFER_SIZE / (sizeof(int16_t) * wavfile->format.num_channels));
            progress = 0;
            while (progress < wavfile->format.num_samples) {
                const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
                const uint32_t num_output_smpls = num_process_smpls * wavfile->format.num_channels;
                buffer = (int16_t *)writer->buffer.bytes;
                for (smpl = 0; smpl < num_process_smpls; smpl++) {
                    for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                        (*buffer++) = (int16_t)(WAVFile_PCM(wavfile, progress + smpl, ch) & 0xFFFF);
                    }
                }
                if (WAVWriter_FWriteLittleEndian(writer->buffer.bytes,
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
            const uint32_t num_output_smpls_per_buffer = (uint32_t)(WAVBITBUFFER_BUFFER_SIZE / (int24_size * wavfile->format.num_channels));
            progress = 0;
            while (progress < wavfile->format.num_samples) {
                const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
                const uint32_t num_output_smpls = num_process_smpls * wavfile->format.num_channels;
                const size_t output_size = num_output_smpls * int24_size;
                buffer = (uint8_t *)writer->buffer.bytes;
                for (smpl = 0; smpl < num_process_smpls; smpl++) {
                    for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                        const int32_t pcm = WAVFile_PCM(wavfile, progress + smpl, ch);
                        (*buffer++) = (uint8_t)((pcm >>  0) & 0xFF);
                        (*buffer++) = (uint8_t)((pcm >>  8) & 0xFF);
                        (*buffer++) = (uint8_t)((pcm >> 16) & 0xFF);
                    }
                }
                if (WAVWriter_FWriteLittleEndian(writer->buffer.bytes,
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
            const uint32_t num_output_smpls_per_buffer = (uint32_t)(WAVBITBUFFER_BUFFER_SIZE / (sizeof(int32_t) * wavfile->format.num_channels));
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
                if (WAVWriter_FWriteLittleEndian(writer->buffer.bytes,
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

/* AIFFファイルのヘッダ部を出力 */
static WAVError WAVWriter_PutAIFFHeader(
    struct WAVWriter *writer, const struct WAVFormat *format)
{
    int32_t inst_exist;
    uint32_t filesize, pcm_data_size;

    /* 引数チェック */
    if ((writer == NULL) || (format == NULL)) {
        return WAV_ERROR_INVALID_PARAMETER;
    }

    /* ループが存在するか？ */
    inst_exist = (format->u.aiff_instrument.loop_begin != format->u.aiff_instrument.loop_end);

    /* PCM データサイズ */
    pcm_data_size
        = format->num_samples * (format->bits_per_sample / 8) * format->num_channels;

    /* ファイルサイズ */
    /* 54はファイル先頭からデータ先頭までのバイト数 */
    filesize = pcm_data_size + 54;
    if (inst_exist) {
        filesize += 46;
    }

    /* ヘッダ 'F', 'O', 'R', 'M' を出力 */
    if (WAVWriter_PutBits(writer, 'F', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'O', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'R', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'M', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ファイルサイズ-8（このフィールド以降のサイズ） */
    if (WAVWriter_PutBigEndianBytes(writer, 4, filesize - 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* ヘッダ 'A', 'I', 'F', 'F' を出力 */
    if (WAVWriter_PutBits(writer, 'A', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'I', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'F', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'F', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* COMMチャンク書き出し */
    if (WAVWriter_PutBits(writer, 'C', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'O', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'M', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'M', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    /* COMMチャンクサイズ（18固定） */
    if (WAVWriter_PutBigEndianBytes(writer, 4, 18) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    /* チャンネル数 */
    if (WAVWriter_PutBigEndianBytes(writer, 2, format->num_channels) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    /* サンプル数 */
    if (WAVWriter_PutBigEndianBytes(writer, 4, format->num_samples) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    /* サンプルあたりのビット数 */
    if (WAVWriter_PutBigEndianBytes(writer, 2, format->bits_per_sample) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    /* サンプリングレート */
    {
        int32_t i;
        uint8_t buf[10] = { 0, };
        if (WAV_MakeAIFFSamplingRate((double)format->sampling_rate, buf, sizeof(buf)) != WAV_ERROR_OK) {
            return WAV_ERROR_INVALID_FORMAT;
        }
        for (i = 0; i < 10; i++) {
            if (WAVWriter_PutBits(writer, buf[i], 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        }
    }

    /* ループが設定されているときはMARK,INSTチャンクを書き込む */
    if (inst_exist) {
        /* MARKチャンクヘッダ */
        if (WAVWriter_PutBits(writer, 'M', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        if (WAVWriter_PutBits(writer, 'A', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        if (WAVWriter_PutBits(writer, 'R', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        if (WAVWriter_PutBits(writer, 'K', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* MARKチャンクサイズ */
        if (WAVWriter_PutBigEndianBytes(writer, 4, 18) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* マーカー数 */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 2) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ループ開始ID */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 1) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ループ開始サンプル位置 */
        if (WAVWriter_PutBigEndianBytes(writer, 4, format->u.aiff_instrument.loop_begin) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ループ開始文字列 */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ループ終了ID */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 2) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ループ終了サンプル位置 */
        if (WAVWriter_PutBigEndianBytes(writer, 4, format->u.aiff_instrument.loop_end) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ループ終了文字列 */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

        /* INSTチャンクヘッダ */
        if (WAVWriter_PutBits(writer, 'I', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        if (WAVWriter_PutBits(writer, 'N', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        if (WAVWriter_PutBits(writer, 'S', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        if (WAVWriter_PutBits(writer, 'T', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* INSTチャンクサイズ */
        if (WAVWriter_PutBigEndianBytes(writer, 4, 20) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* キー（ノート）番号 */
        if (WAVWriter_PutBigEndianBytes(writer, 1, format->u.aiff_instrument.key) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* デチューン（0埋め） */
        if (WAVWriter_PutBigEndianBytes(writer, 1, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* 最小ノート番号（0埋め） */
        if (WAVWriter_PutBigEndianBytes(writer, 1, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* 最大ノート番号（0埋め） */
        if (WAVWriter_PutBigEndianBytes(writer, 1, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* 最小ベロシティ（0埋め） */
        if (WAVWriter_PutBigEndianBytes(writer, 1, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* 最大ベロシティ（0埋め） */
        if (WAVWriter_PutBigEndianBytes(writer, 1, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ゲイン */
        if (WAVWriter_PutBigEndianBytes(writer, 2, format->u.aiff_instrument.gain) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* サステインループ */
        /* ループ再生モード（0埋め） */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ループ開始マーカーID */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 1) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* ループ終点マーカーID */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 2) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        /* リリースループ（0埋め） */
        if (WAVWriter_PutBigEndianBytes(writer, 2, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        if (WAVWriter_PutBigEndianBytes(writer, 2, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
        if (WAVWriter_PutBigEndianBytes(writer, 2, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    }

    /* SSNDチャンクのヘッダ出力 */
    if (WAVWriter_PutBits(writer, 'S', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'S', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'N', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    if (WAVWriter_PutBits(writer, 'D', 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    /* 波形データバイト数（コメントサイズとブロックサイズの8バイトを加算） */
    if (WAVWriter_PutBigEndianBytes(writer, 4, pcm_data_size + 8) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    /* コメントサイズ */
    if (WAVWriter_PutBigEndianBytes(writer, 4, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }
    /* ブロックサイズ */
    if (WAVWriter_PutBigEndianBytes(writer, 4, 0) != WAV_ERROR_OK) { return WAV_ERROR_IO; }

    return WAV_ERROR_OK;
}

/* AIFFファイルのPCMデータ出力 */
static WAVError WAVWriter_PutAIFFPcmData(
    struct WAVWriter *writer, const struct WAVFile *wavfile)
{
    uint32_t ch, smpl, progress;

    /* バッファは空に */
    WAVWriter_Flush(writer);

    /* チャンネルインターリーブしながらビッグエンディアンで書き出し */
    switch (wavfile->format.bits_per_sample) {
    case 8:
    {
        uint8_t *buffer;
        const uint32_t num_output_smpls_per_buffer = WAVBITBUFFER_BUFFER_SIZE / (sizeof(uint8_t) * wavfile->format.num_channels);
        progress = 0;
        while (progress < wavfile->format.num_samples) {
            const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
            const size_t output_size = num_process_smpls * wavfile->format.num_channels * sizeof(uint8_t);
            buffer = (uint8_t *)writer->buffer.bytes;
            for (smpl = 0; smpl < num_process_smpls; smpl++) {
                for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                    (*buffer++) = (uint8_t)((WAVFile_PCM(wavfile, progress + smpl, ch) + 128) & 0xFF);
                }
            }
            if (fwrite(writer->buffer.bytes,
                sizeof(uint8_t), output_size, writer->fp) < output_size) {
                return WAV_ERROR_IO;
            }
            progress += num_process_smpls;
        }
    }
    break;
    case 16:
    {
        const uint32_t num_output_smpls_per_buffer = (uint32_t)(WAVBITBUFFER_BUFFER_SIZE / (sizeof(int16_t) * wavfile->format.num_channels));
        progress = 0;
        while (progress < wavfile->format.num_samples) {
            const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
            const size_t output_size = num_process_smpls * wavfile->format.num_channels * sizeof(int16_t);
            uint8_t *buffer = (uint8_t *)writer->buffer.bytes;
            for (smpl = 0; smpl < num_process_smpls; smpl++) {
                for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                    const int32_t pcm = WAVFile_PCM(wavfile, progress + smpl, ch);
                    (*buffer++) = (uint8_t)((pcm >> 8) & 0xFF);
                    (*buffer++) = (uint8_t)((pcm >> 0) & 0xFF);
                }
            }
            if (fwrite(writer->buffer.bytes,
                sizeof(uint8_t), output_size, writer->fp) < output_size) {
                return WAV_ERROR_IO;
            }
            progress += num_process_smpls;
        }
    }
    break;
    case 24:
    {
        const size_t int24_size = 3 * sizeof(uint8_t);
        const uint32_t num_output_smpls_per_buffer = (uint32_t)(WAVBITBUFFER_BUFFER_SIZE / (int24_size * wavfile->format.num_channels));
        progress = 0;
        while (progress < wavfile->format.num_samples) {
            const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
            const uint32_t num_output_smpls = num_process_smpls * wavfile->format.num_channels;
            const size_t output_size = num_output_smpls * int24_size;
            uint8_t *buffer = (uint8_t *)writer->buffer.bytes;
            for (smpl = 0; smpl < num_process_smpls; smpl++) {
                for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                    const int32_t pcm = WAVFile_PCM(wavfile, progress + smpl, ch);
                    (*buffer++) = (uint8_t)((pcm >> 16) & 0xFF);
                    (*buffer++) = (uint8_t)((pcm >>  8) & 0xFF);
                    (*buffer++) = (uint8_t)((pcm >>  0) & 0xFF);
                }
            }
            if (fwrite(writer->buffer.bytes,
                sizeof(uint8_t), output_size, writer->fp) < output_size) {
                return WAV_ERROR_IO;
            }
            progress += num_process_smpls;
        }
    }
    break;
    case 32:
    {
        const uint32_t num_output_smpls_per_buffer = (uint32_t)(WAVBITBUFFER_BUFFER_SIZE / (sizeof(int32_t) * wavfile->format.num_channels));
        progress = 0;
        while (progress < wavfile->format.num_samples) {
            const uint32_t num_process_smpls = WAV_Min(num_output_smpls_per_buffer, wavfile->format.num_samples - progress);
            const size_t output_size = num_process_smpls * wavfile->format.num_channels * sizeof(int32_t);
            uint8_t *buffer = (uint8_t *)writer->buffer.bytes;
            for (smpl = 0; smpl < num_process_smpls; smpl++) {
                for (ch = 0; ch < wavfile->format.num_channels; ch++) {
                    const int32_t pcm = WAVFile_PCM(wavfile, progress + smpl, ch);
                    (*buffer++) = (uint8_t)((pcm >> 24) & 0xFF);
                    (*buffer++) = (uint8_t)((pcm >> 16) & 0xFF);
                    (*buffer++) = (uint8_t)((pcm >>  8) & 0xFF);
                    (*buffer++) = (uint8_t)((pcm >>  0) & 0xFF);
                }
            }
            if (fwrite(writer->buffer.bytes,
                sizeof(uint8_t), output_size, writer->fp) < output_size) {
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
    struct WAVWriter writer;
    FILE* fp;

    /* 引数チェック */
    if ((filename == NULL) || (wavfile == NULL)) {
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

    switch (wavfile->format.file_format) {
    case WAV_FILEFORMAT_PCMWAVEFORMAT:
    case WAV_FILEFORMAT_WAVEFORMATEXTENSIBLE:
        /* ヘッダ書き出し */
        if (WAVWriter_PutWAVHeader(&writer, &wavfile->format) != WAV_ERROR_OK) {
            return WAV_APIRESULT_NG;
        }
        /* データ書き出し */
        if (WAVWriter_PutWAVPcmData(&writer, wavfile) != WAV_ERROR_OK) {
            return WAV_APIRESULT_NG;
        }
        break;
    case WAV_FILEFORMAT_AIFF:
        /* ヘッダ書き出し */
        if (WAVWriter_PutAIFFHeader(&writer, &wavfile->format) != WAV_ERROR_OK) {
            return WAV_APIRESULT_NG;
        }
        /* データ書き出し */
        if (WAVWriter_PutAIFFPcmData(&writer, wavfile) != WAV_ERROR_OK) {
            return WAV_APIRESULT_NG;
        }
        break;
    default:
        return WAV_APIRESULT_INVALID_FORMAT;
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
    writer->fp = fp;
    writer->bit_count = 8;
    writer->bit_buffer = 0;
    memset(&writer->buffer, 0, sizeof(struct WAVBitBuffer));
    writer->buffer.byte_pos = 0;
}

/* ライタの終了 */
static void WAVWriter_Finalize(struct WAVWriter* writer)
{
    /* バッファに余っているデータを書き出し */
    WAVWriter_Flush(writer);

    /* メンバをクリア */
    writer->fp = NULL;
    writer->bit_count = 8;
    writer->bit_buffer = 0;
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

/* ビッグエンディアンでビットパターンを出力 */
static WAVError WAVWriter_PutBigEndianBytes(
    struct WAVWriter *writer, uint32_t nbytes, uint64_t data)
{
    /* 出力 */
    if (WAVWriter_PutBits(writer, data, (uint8_t)(nbytes * 8)) != WAV_ERROR_OK) {
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

    assert(nbytes <= 8);

    /* ビッグエンディアンで取得 */
    if (WAVParser_GetBits(parser, nbytes * 8, &tmp) != WAV_ERROR_OK) {
        return WAV_ERROR_IO;
    }

    /* リトルエンディアンに並び替え */
    ret = 0;
    for (i_byte = 0; i_byte < nbytes; i_byte++) {
        ret |= ((tmp >> (8 * (nbytes - i_byte - 1))) & 0xFFUL) << (8 * i_byte);
    }

    (*bitsbuf) = ret;

    return WAV_ERROR_OK;
}

/* ビックエンディアンでビットパターンを取得 */
static WAVError WAVParser_GetBigEndianBytes(
    struct WAVParser *parser, uint32_t nbytes, uint64_t *bitsbuf)
{
    uint64_t tmp;
    uint32_t i_byte;

    assert(nbytes <= 8);

    /* ビッグエンディアンで取得 */
    if (WAVParser_GetBits(parser, nbytes * 8, &tmp) != WAV_ERROR_OK) {
        return WAV_ERROR_IO;
    }

    (*bitsbuf) = tmp;

    return WAV_ERROR_OK;
}

/* パーサを使用して文字列取得 */
static WAVError WAVParser_GetString(
        struct WAVParser* parser, char* string_buffer, uint32_t string_length)
{
    uint32_t i_byte;
    uint64_t bitsbuf;

    assert((parser != NULL) && (string_buffer != NULL));

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

    assert((parser != NULL) && (signature != NULL));

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

/* AIFFのサンプリングレートを取得 */
static WAVError WAV_ParseAIFFSamplingRate(const uint8_t *data, size_t data_size, double *sampling_rate)
{
#define U32TODOUBLE(u32) (((double)((int32_t)((u32) - 2147483647L - 1))) + 2147483648.0)
    int32_t exp;
    uint32_t high_mant, low_mant;
    double tmp_rate;

    assert((data != NULL) && (sampling_rate != NULL));

    /* サイズ不足 */
    if (data_size < 10) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* 指数部と仮数部の読み取り */
    exp = ((data[0] & 0x7F) << 8) | data[1];
    high_mant = ((uint32_t)data[2] << 24)
        | ((uint32_t)data[3] << 16)
        | ((uint32_t)data[4] <<  8)
        | ((uint32_t)data[5] <<  0);
    low_mant = ((uint32_t)data[6] << 24)
        | ((uint32_t)data[7] << 16)
        | ((uint32_t)data[8] <<  8)
        | ((uint32_t)data[9] <<  0);

    /* 無限大はサポート外 */
    if (exp == 0x7FFF) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* 0の場合 */
    if ((exp == 0) && (high_mant == 0) && (low_mant == 0)) {
        (*sampling_rate) = 0;
        return WAV_ERROR_OK;
    }

    /* 浮動小数点数の読み取り */
    exp -= 16383;
    tmp_rate = U32TODOUBLE(high_mant) * pow(2.0, exp - 31);
    tmp_rate += U32TODOUBLE(low_mant) * pow(2.0, exp - 63);

    /* 符号ビットを反映 */
    if (data[0] & 0x80) {
        tmp_rate = -tmp_rate;
    }

    (*sampling_rate) = tmp_rate;
    return WAV_ERROR_OK;
}

/* AIFFのサンプリングレートを書き出し */
static WAVError WAV_MakeAIFFSamplingRate(double sampling_rate, uint8_t *data, size_t data_size)
{
#define DOUBLETOU32(d) ((uint32_t)(((int32_t)((d) - 2147483648.0)) + 2147483647L) + 1)
    int32_t sign, exp;
    uint32_t high_mant, low_mant;
    double tmp_rate, double_mant, floor_mant;

    assert(data != NULL);

    /* サイズ不足 */
    if (data_size < 10) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    /* レートが0の場合 */
    if (sampling_rate == 0.0) {
        int32_t i;
        for (i = 0; i < 10; i++) {
            data[i] = 0;
        }
        return WAV_ERROR_OK;
    }

    /* 符号ビットの反映 */
    if (sampling_rate < 0.0) {
        sign = 1;
        tmp_rate = -sampling_rate;
    } else {
        sign = 0;
        tmp_rate = sampling_rate;
    }

    /* 指数部と仮数部に分ける */
    double_mant = frexp(tmp_rate, &exp);

    /* 無限大あるいはNaNはエラーとする */
    if ((exp > 16384) || !(double_mant < 1)) {
        return WAV_ERROR_INVALID_FORMAT;
    }

    exp += 16382;

    /* 非正規化数 */
    if (exp < 0) {
        double_mant *= pow(2.0, exp);
        exp = 0;
    }

    /* 仮数部の設定 */
    double_mant *= pow(2.0, 32.0);
    floor_mant = floor(double_mant);
    high_mant = DOUBLETOU32(floor_mant);
    double_mant = (double_mant - floor_mant) * pow(2.0, 32.0);
    floor_mant = floor(double_mant);
    low_mant = DOUBLETOU32(floor_mant);

    /* 書き出し */
    data[0] = (sign << 7) | ((exp >> 8) & 0x7F);
    data[1] = exp & 0xFF;
    data[2] = (high_mant >> 24) & 0xFF;
    data[3] = (high_mant >> 16) & 0xFF;
    data[4] = (high_mant >>  8) & 0xFF;
    data[5] = (high_mant >>  0) & 0xFF;
    data[6] = (low_mant >> 24) & 0xFF;
    data[7] = (low_mant >> 16) & 0xFF;
    data[8] = (low_mant >> 8) & 0xFF;
    data[9] = (low_mant >> 0) & 0xFF;

    return WAV_ERROR_OK;
}
