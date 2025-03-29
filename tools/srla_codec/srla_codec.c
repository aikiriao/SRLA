#include <srla_encoder.h>
#include <srla_decoder.h>
#include "wav.h"
#include "command_line_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* デフォルトプリセット */
#define DEFALUT_PRESET_INDEX 3
/* デフォルトの最大ブロックサンプル数 */
#define DEFALUT_MAX_NUM_BLOCK_SAMPLES 4096
/* デフォルトの先読みサンプル数倍率 */
#define DEFALUT_LOOKAHEAD_SAMPLES_FACTOR 4
/* デフォルトの可変ブロック分割数 */
#define DEFALUT_NUM_VARIABLE_BLOCK_DIVISIONS 1
/* パラメータプリセットの最大インデックス */
#define SRLA_MAX_PARAMETER_PRESETS_INDEX 6
#if SRLA_MAX_PARAMETER_PRESETS_INDEX != (SRLA_NUM_PARAMETER_PRESETS - 1)
#error "Max parameter presets mismatched to number of parameter presets!"
#endif
/* マクロの内容を文字列化 */
#define PRE_TOSTRING(arg) #arg
#define TOSTRING(arg) PRE_TOSTRING(arg)

/* a, bのうち小さい方を選択 */
#define SRLACODEC_MIN(a, b) (((a) < (b)) ? (a) : (b))

/* コマンドライン仕様 */
static struct CommandLineParserSpecification command_line_spec[] = {
    { 'e', "encode", "Encode mode",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'd', "decode", "Decode mode",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'm', "mode", "Specify compress mode: 0(fast), ..., " TOSTRING(SRLA_MAX_PARAMETER_PRESETS_INDEX) "(high compression) (default:" TOSTRING(DEFALUT_PRESET_INDEX) ")",
        COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'L', "lookahead-sample-factor", "Specify the multiply factor for lookahead samples in variable block division (default:" TOSTRING(DEFALUT_LOOKAHEAD_SAMPLES_FACTOR) ")",
        COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'B', "max-block-size", "Specify max number of block samples (default:" TOSTRING(DEFALUT_MAX_NUM_BLOCK_SAMPLES) ")",
        COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'V', "variable-block-divisions", "Specify number of variable block-size divisions (default:" TOSTRING(DEFALUT_NUM_VARIABLE_BLOCK_DIVISIONS) ")",
        COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'P', "enable-long-term-prediction", "Enable long term (pitch) prediction (default:disabled)",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    {   0, "no-checksum-check", "Whether to NOT check checksum at decoding (default:no)",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'h', "help", "Show command help message",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'v', "version", "Show version information",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 0, }
};

/* ブロックエンコードコールバック */
static void encode_block_callback(
    uint32_t num_samples, uint32_t progress_samples, const uint8_t *encoded_block_data, uint32_t block_data_size)
{
    /* 進捗表示 */
    printf("progress... %5.2f%% \r", (double)((progress_samples * 100.0) / num_samples));
    fflush(stdout);
}

/* エンコード 成功時は0、失敗時は0以外を返す */
static int do_encode(const char *in_filename, const char *out_filename,
    uint32_t encode_preset_no, uint32_t max_num_block_samples, uint32_t variable_block_num_divisions, uint32_t lookahead_samples_factor, SRLAEncoderLTPMode ltp_mode)
{
    FILE *out_fp;
    struct WAVFile *in_wav;
    struct SRLAEncoder *encoder;
    struct SRLAEncoderConfig config;
    struct SRLAEncodeParameter parameter;
    struct stat fstat;
    uint8_t *buffer;
    uint32_t buffer_size, encoded_data_size;
    SRLAApiResult ret;
    uint32_t ch, smpl, num_channels, num_samples;

    /* エンコーダ作成 */
    config.max_num_channels = SRLA_MAX_NUM_CHANNELS;
    config.min_num_samples_per_block = max_num_block_samples >> variable_block_num_divisions;
    config.max_num_samples_per_block = max_num_block_samples;
    config.max_num_lookahead_samples = lookahead_samples_factor * max_num_block_samples;
    config.max_num_parameters = SRLA_MAX_COEFFICIENT_ORDER;
    if ((encoder = SRLAEncoder_Create(&config, NULL, 0)) == NULL) {
        fprintf(stderr, "Failed to create encoder handle. \n");
        return 1;
    }

    /* WAVファイルオープン */
    if ((in_wav = WAV_CreateFromFile(in_filename)) == NULL) {
        fprintf(stderr, "Failed to open %s. \n", in_filename);
        return 1;
    }
    num_channels = in_wav->format.num_channels;
    num_samples = in_wav->format.num_samples;

    /* エンコードパラメータセット */
    parameter.num_channels = (uint16_t)num_channels;
    parameter.bits_per_sample = (uint16_t)in_wav->format.bits_per_sample;
    parameter.sampling_rate = in_wav->format.sampling_rate;
    parameter.min_num_samples_per_block = max_num_block_samples >> variable_block_num_divisions;
    parameter.max_num_samples_per_block = max_num_block_samples;
    parameter.num_lookahead_samples = lookahead_samples_factor * max_num_block_samples;
    parameter.ltp_mode = ltp_mode;
    /* プリセットの反映 */
    parameter.preset = (uint8_t)encode_preset_no;
    if ((ret = SRLAEncoder_SetEncodeParameter(encoder, &parameter)) != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Failed to set encode parameter: %d \n", ret);
        return 1;
    }

    /* 入力ファイルのサイズを拾っておく */
    stat(in_filename, &fstat);
    /* 入力wavの2倍よりは大きくならないだろうという想定 */
    buffer_size = (uint32_t)(2 * fstat.st_size);

    /* エンコードデータ領域を作成 */
    buffer = (uint8_t *)malloc(buffer_size);

    /* エンコード実行 */
    if ((ret = SRLAEncoder_EncodeWhole(encoder,
        in_wav->data, num_samples, buffer, buffer_size, &encoded_data_size, encode_block_callback)) != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Failed to encode data: %d \n", ret);
        return 1;
    }

    /* ファイル書き出し */
    out_fp = fopen(out_filename, "wb");
    if (fwrite(buffer, sizeof(uint8_t), encoded_data_size, out_fp) < encoded_data_size) {
        fprintf(stderr, "File output error! %d \n", ret);
        return 1;
    }

    /* 圧縮結果サマリの表示 */
    printf("finished: %d -> %d (%6.2f %%) \n",
            (uint32_t)fstat.st_size, encoded_data_size, (double)((100.0 * encoded_data_size) / (double)fstat.st_size));

    /* リソース破棄 */
    fclose(out_fp);
    free(buffer);
    WAV_Destroy(in_wav);
    SRLAEncoder_Destroy(encoder);

    return 0;
}

/* デコード 成功時は0、失敗時は0以外を返す */
static int do_decode(const char *in_filename, const char *out_filename, uint8_t check_checksum)
{
    FILE* in_fp;
    struct WAVFile* out_wav;
    struct WAVFileFormat wav_format;
    struct stat fstat;
    struct SRLADecoder* decoder;
    struct SRLADecoderConfig config;
    struct SRLAHeader header;
    uint8_t* buffer;
    uint32_t ch, smpl, buffer_size;
    SRLAApiResult ret;

    /* デコーダハンドルの作成 */
    config.max_num_channels = SRLA_MAX_NUM_CHANNELS;
    config.max_num_parameters = SRLA_MAX_COEFFICIENT_ORDER;
    config.check_checksum = check_checksum;
    if ((decoder = SRLADecoder_Create(&config, NULL, 0)) == NULL) {
        fprintf(stderr, "Failed to create decoder handle. \n");
        return 1;
    }

    /* 入力ファイルオープン */
    in_fp = fopen(in_filename, "rb");
    /* 入力ファイルのサイズ取得 / バッファ領域割り当て */
    stat(in_filename, &fstat);
    buffer_size = (uint32_t)fstat.st_size;
    buffer = (uint8_t *)malloc(buffer_size);
    /* バッファ領域にデータをロード */
    fread(buffer, sizeof(uint8_t), buffer_size, in_fp);
    fclose(in_fp);

    /* ヘッダデコード */
    if ((ret = SRLADecoder_DecodeHeader(buffer, buffer_size, &header))
            != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Failed to get header information: %d \n", ret);
        return 1;
    }

    /* 出力wavハンドルの生成 */
    wav_format.data_format     = WAV_DATA_FORMAT_PCM;
    wav_format.num_channels    = header.num_channels;
    wav_format.sampling_rate   = header.sampling_rate;
    wav_format.bits_per_sample = header.bits_per_sample;
    wav_format.num_samples     = header.num_samples;
    if ((out_wav = WAV_Create(&wav_format)) == NULL) {
        fprintf(stderr, "Failed to create wav handle. \n");
        return 1;
    }

    /* 一括デコード */
    if ((ret = SRLADecoder_DecodeWhole(decoder,
                    buffer, buffer_size,
                    (int32_t **)out_wav->data, out_wav->format.num_channels, out_wav->format.num_samples))
                != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Decoding error! %d \n", ret);
        return 1;
    }

    /* WAVファイル書き出し */
    if (WAV_WriteToFile(out_filename, out_wav) != WAV_APIRESULT_OK) {
        fprintf(stderr, "Failed to write wav file. \n");
        return 1;
    }

    free(buffer);
    WAV_Destroy(out_wav);
    SRLADecoder_Destroy(decoder);

    return 0;
}

/* 使用法の表示 */
static void print_usage(char** argv)
{
    printf("Usage: %s [options] INPUT_FILE_NAME OUTPUT_FILE_NAME \n", argv[0]);
}

/* バージョン情報の表示 */
static void print_version_info(void)
{
    printf("SRLA -- SVR-FIR Lossless Audio codec Version.%d \n", SRLA_CODEC_VERSION);
}

/* メインエントリ */
int main(int argc, char** argv)
{
    const char *filename_ptr[2] = { NULL, NULL };
    const char *input_file;
    const char *output_file;

    /* 引数が足らない */
    if (argc == 1) {
        print_usage(argv);
        /* 初めて使った人が詰まらないようにヘルプの表示を促す */
        printf("Type `%s -h` to display command helps. \n", argv[0]);
        return 1;
    }

    /* コマンドライン解析 */
    if (CommandLineParser_ParseArguments(command_line_spec,
                argc, (const char *const *)argv, filename_ptr, sizeof(filename_ptr) / sizeof(filename_ptr[0]))
            != COMMAND_LINE_PARSER_RESULT_OK) {
        return 1;
    }

    /* ヘルプやバージョン情報の表示判定 */
    if (CommandLineParser_GetOptionAcquired(command_line_spec, "help") == COMMAND_LINE_PARSER_TRUE) {
        print_usage(argv);
        printf("options: \n");
        CommandLineParser_PrintDescription(command_line_spec);
        return 0;
    } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "version") == COMMAND_LINE_PARSER_TRUE) {
        print_version_info();
        return 0;
    }

    /* 入力ファイル名の取得 */
    if ((input_file = filename_ptr[0]) == NULL) {
        fprintf(stderr, "%s: input file must be specified. \n", argv[0]);
        return 1;
    }

    /* 出力ファイル名の取得 */
    if ((output_file = filename_ptr[1]) == NULL) {
        fprintf(stderr, "%s: output file must be specified. \n", argv[0]);
        return 1;
    }

    /* エンコードとデコードは同時に指定できない */
    if ((CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_TRUE)
            && (CommandLineParser_GetOptionAcquired(command_line_spec, "encode") == COMMAND_LINE_PARSER_TRUE)) {
        fprintf(stderr, "%s: encode and decode mode cannot specify simultaneously. \n", argv[0]);
        return 1;
    }

    if (CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_TRUE) {
        /* デコード */
        uint8_t crc_check = 1;
        /* CRC無効フラグを取得 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "no-crc-check") == COMMAND_LINE_PARSER_TRUE) {
            crc_check = 0;
        }
        /* 一括デコード実行 */
        if (do_decode(input_file, output_file, crc_check) != 0) {
            fprintf(stderr, "%s: failed to decode %s. \n", argv[0], input_file);
            return 1;
        }
    } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "encode") == COMMAND_LINE_PARSER_TRUE) {
        /* エンコード */
        uint32_t encode_preset_no = DEFALUT_PRESET_INDEX;
        uint32_t max_num_block_samples = DEFALUT_MAX_NUM_BLOCK_SAMPLES;
        uint32_t variable_block_num_divisions = DEFALUT_NUM_VARIABLE_BLOCK_DIVISIONS;
        uint32_t lookahead_samples_factor = DEFALUT_LOOKAHEAD_SAMPLES_FACTOR;
        SRLAEncoderLTPMode ltp_mode = SRLAENCODER_LTP_DISABLED;
        /* エンコードプリセット番号取得 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "mode") == COMMAND_LINE_PARSER_TRUE) {
            char *e;
            const char *lstr = CommandLineParser_GetArgumentString(command_line_spec, "mode");
            encode_preset_no = (uint32_t)strtol(lstr, &e, 10);
            if (*e != '\0') {
                fprintf(stderr, "%s: invalid encode preset number. (irregular character found in %s at %s)\n", argv[0], lstr, e);
                return 1;
            }
            if (encode_preset_no >= SRLA_NUM_PARAMETER_PRESETS) {
                fprintf(stderr, "%s: encode preset number is out of range. \n", argv[0]);
                return 1;
            }
        }
        /* ブロックあたりサンプル数の取得 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "lookahead-sample-factor") == COMMAND_LINE_PARSER_TRUE) {
            char* e;
            const char* lstr = CommandLineParser_GetArgumentString(command_line_spec, "lookahead-sample-factor");
            lookahead_samples_factor = (uint32_t)strtol(lstr, &e, 10);
            if (*e != '\0') {
                fprintf(stderr, "%s: invalid number of lookahead samples. (irregular character found in %s at %s)\n", argv[0], lstr, e);
                return 1;
            }
            if ((lookahead_samples_factor == 0) || (lookahead_samples_factor >= (1U << 16))) {
                fprintf(stderr, "%s: lookahead factor is out of range. \n", argv[0]);
                return 1;
            }
        }
        /* ブロックあたりサンプル数の取得 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "max-block-size") == COMMAND_LINE_PARSER_TRUE) {
            char *e;
            const char *lstr = CommandLineParser_GetArgumentString(command_line_spec, "max-block-size");
            max_num_block_samples = (uint32_t)strtol(lstr, &e, 10);
            if (*e != '\0') {
                fprintf(stderr, "%s: invalid number of block samples. (irregular character found in %s at %s)\n", argv[0], lstr, e);
                return 1;
            }
            if ((max_num_block_samples == 0) || (max_num_block_samples >= (1U << 16))) {
                fprintf(stderr, "%s: number of block samples is out of range. \n", argv[0]);
                return 1;
            }
        }
        /* 可変ブロックエンコード分割数 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "variable-block-divisions") == COMMAND_LINE_PARSER_TRUE) {
            char *e;
            const char *lstr = CommandLineParser_GetArgumentString(command_line_spec, "variable-block-divisions");
            variable_block_num_divisions = (uint32_t)strtol(lstr, &e, 10);
            if (*e != '\0') {
                fprintf(stderr, "%s: invalid number of variable block divisions. (irregular character found in %s at %s)\n", argv[0], lstr, e);
                return 1;
            }
            if ((max_num_block_samples >> variable_block_num_divisions) == 0) {
                fprintf(stderr, "%s: number of variable block divisions is too large. \n", argv[0]);
                return 1;
            }
        }
        /* LTP動作モード */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "enable-long-term-prediction") == COMMAND_LINE_PARSER_TRUE) {
            ltp_mode = SRLAENCODER_LTP_ENABLED;
        }
        /* 一括エンコード実行 */
        if (do_encode(input_file, output_file,
            encode_preset_no, max_num_block_samples, variable_block_num_divisions, lookahead_samples_factor, ltp_mode) != 0) {
            fprintf(stderr, "%s: failed to encode %s. \n", argv[0], input_file);
            return 1;
        }
    } else {
        fprintf(stderr, "%s: decode(-d) or encode(-e) option must be specified. \n", argv[0]);
        return 1;
    }

    return 0;
}
