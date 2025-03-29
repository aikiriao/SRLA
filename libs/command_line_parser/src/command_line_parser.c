#include "command_line_parser.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* 仕様リストサイズを計測 */
static uint32_t CommandLineParser_GetNumSpecifications(
        const struct CommandLineParserSpecification* clps)
{
    uint32_t num_specs;

    assert(clps != NULL);

    /* リスト終端の0にぶつかるまでポインタを進める */
    num_specs = 0;
    while (clps->long_option != NULL) {
        num_specs++;
        clps++;
    }

    return num_specs;
}

/* コマンドラインパーサ仕様のチェック */
static CommandLineParserBool CommandLineParser_CheckSpecification(
        const struct CommandLineParserSpecification* clps)
{
    uint32_t spec_no;
    uint32_t num_specs;

    assert(clps != NULL);

    /* 仕様数の取得 */
    num_specs = CommandLineParser_GetNumSpecifications(clps);

    for (spec_no = 0; spec_no < num_specs; spec_no++) {
        uint32_t j;
        for (j = 0; j < num_specs; j++) {
            if (j == spec_no) {
                continue;
            }
            /* 同じオプション文字列を持つものがいたら不正 */
            if ((clps[j].short_option != 0) && (clps[j].short_option == clps[spec_no].short_option)) {
                return COMMAND_LINE_PARSER_FALSE;
            } else if ((clps[j].long_option != NULL) && (clps[spec_no].long_option != NULL)) {
                if (strcmp(clps[j].long_option, clps[spec_no].long_option) == 0) {
                    return COMMAND_LINE_PARSER_FALSE;
                }
            }
        }
        /* ロングオプションの設定は必須 */
        if (clps[spec_no].long_option == NULL) {
            return COMMAND_LINE_PARSER_FALSE;
        }
    }

    /* 問題なし */
    return COMMAND_LINE_PARSER_TRUE;
}

/* 引数説明の印字 */
void CommandLineParser_PrintDescription(const struct CommandLineParserSpecification* clps)
{
    uint32_t  spec_no;
    char      command_str[256];
    uint32_t  num_specs;

    /* 引数チェック */
    if (clps == NULL) {
        fprintf(stderr, "Pointer to command-line specification is NULL. \n");
        return;
    }

    /* 仕様をチェックしておく */
    if (CommandLineParser_CheckSpecification(clps) != COMMAND_LINE_PARSER_TRUE) {
        fprintf(stderr, "Warning: Command-line specification is invalid. (Unable to parse) \n");
    }

    /* 仕様数の取得 */
    num_specs = CommandLineParser_GetNumSpecifications(clps);

    /* 仕様を順番に表示 */
    for (spec_no = 0; spec_no < num_specs; spec_no++) {
        const struct CommandLineParserSpecification* pspec = &clps[spec_no];

        /* コマンド文字列を作成 */
        if (pspec->short_option != 0) {
            sprintf(command_str, "  -%c, --%s", pspec->short_option, pspec->long_option);
        } else {
            sprintf(command_str, "      --%s", pspec->long_option);
        }

        /* 引数の属性文字列を作成 */
        if (pspec->need_argument == COMMAND_LINE_PARSER_TRUE) {
            strcat(command_str, " (needs argument)");
        } 

        /* 説明を付加して全てを印字 */
        printf("%-30s %s \n",
                command_str, (pspec->description != NULL) ? pspec->description : "");
    }
}

/* オプション名からインデックスを取得 */
static CommandLineParserResult CommandLineParser_GetSpecificationIndex(
        const struct CommandLineParserSpecification* clps,
        const char* option_name, uint32_t* index)
{
    uint32_t spec_no;
    uint32_t num_specs;

    /* 引数チェック */
    if (clps == NULL || option_name == NULL || index == NULL) {
        return COMMAND_LINE_PARSER_RESULT_INVALID_ARGUMENT;
    }

    /* 仕様数の取得 */
    num_specs = CommandLineParser_GetNumSpecifications(clps);

    /* ショートオプションから検索 */
    if (strlen(option_name) == 1) {
        for (spec_no = 0; spec_no < num_specs; spec_no++) {
            if (option_name[0] == clps[spec_no].short_option) {
                *index = spec_no;
                return COMMAND_LINE_PARSER_RESULT_OK;
            }
        }
    }

    /* ロングオプションから検索 */
    for (spec_no = 0; spec_no < num_specs; spec_no++) {
        if (strcmp(option_name, clps[spec_no].long_option) == 0) {
            *index = spec_no;
            return COMMAND_LINE_PARSER_RESULT_OK;
        }
    }

    /* 見つからなかった */
    return COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION;
}

/* オプション名からそのオプションが指定されたか取得 */
CommandLineParserBool CommandLineParser_GetOptionAcquired(
        const struct CommandLineParserSpecification* clps,
        const char* option_name)
{
    uint32_t spec_no;

    /* インデックス取得 */
    if (CommandLineParser_GetSpecificationIndex(clps, option_name, &spec_no) != COMMAND_LINE_PARSER_RESULT_OK) {
        return COMMAND_LINE_PARSER_FALSE;
    }

    return clps[spec_no].acquired;
}

/* オプション名からそのオプション引数を取得 */
const char* CommandLineParser_GetArgumentString(
        const struct CommandLineParserSpecification* clps,
        const char* option_name)
{
    uint32_t spec_no;

    /* インデックス取得 */
    if (CommandLineParser_GetSpecificationIndex(clps, option_name, &spec_no) != COMMAND_LINE_PARSER_RESULT_OK) {
        return NULL;
    }

    return clps[spec_no].argument_string;
}

/* 引数のパース */
CommandLineParserResult CommandLineParser_ParseArguments(
        struct CommandLineParserSpecification* clps,
        int32_t argc, const char* const* argv,
        const char** other_string_array, uint32_t other_string_array_size)
{
    int32_t     count;
    uint32_t    spec_no;
    const char* arg_str;
    uint32_t    other_string_index;
    uint32_t    num_specs;

    /* 引数チェック */
    if (argv == NULL || clps == NULL) {
        return COMMAND_LINE_PARSER_RESULT_INVALID_ARGUMENT;
    }

    /* 仕様数の取得 */
    num_specs = CommandLineParser_GetNumSpecifications(clps);

    /* コマンドライン仕様のチェック */
    if (CommandLineParser_CheckSpecification(clps) != COMMAND_LINE_PARSER_TRUE) {
        return COMMAND_LINE_PARSER_RESULT_INVALID_SPECIFICATION;
    }

    /* 全てのオプションを未取得状態にセット */
    for (spec_no = 0; spec_no < num_specs; spec_no++) {
        clps[spec_no].acquired = COMMAND_LINE_PARSER_FALSE;
    }

    /* argv[0]はプログラム名だから飛ばす */
    other_string_index = 0;
    for (count = 1; count < argc; count++) {
        /* 文字列配列の要素を取得 */
        arg_str = argv[count];
        /* オプション文字列を検査 */
        if (strncmp(arg_str, "--", 2) == 0) {
            /* ロングオプション */
            for (spec_no = 0; spec_no < num_specs; spec_no++) {
                uint32_t long_option_len;
                struct CommandLineParserSpecification* pspec = &clps[spec_no];
                /* ロングオプション文字列がNULLなら飛ばす */
                if (pspec->long_option == NULL) {
                    continue;
                }
                long_option_len = (uint32_t)strlen(pspec->long_option);
                if (strncmp(&arg_str[2], pspec->long_option, long_option_len) == 0) {
                    /* ロングオプションの後はナル終端かオプション指定のための'='が来なければならない */
                    if (arg_str[2 + long_option_len] == '\0') {
                        /* 既に取得済みのオプションが指定された */
                        if (pspec->acquired == COMMAND_LINE_PARSER_TRUE) {
                            fprintf(stderr, "%s: Option \"%s\" multiply specified. \n", argv[0], pspec->long_option);
                            return COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED;
                        }
                        if (pspec->need_argument == COMMAND_LINE_PARSER_TRUE) {
                            /* 引数を取るオプションの場合は、そのまま引数を取りに行く */
                            if ((count + 1) == argc) {
                                /* 終端に達している */
                                fprintf(stderr, "%s: Option \"%s\" needs argument. \n", argv[0], pspec->long_option);
                                return COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION;
                            } else if ((strncmp(argv[count + 1], "--", 2) == 0) || argv[count + 1][0] == '-') {
                                /* 他のオプション指定が入っている */
                                /* （オプション引数文字列として"--", '-'が先頭にくるものは認めない） */
                                fprintf(stderr, "%s: Option \"%s\" needs argument. \n", argv[0], pspec->long_option);
                                return COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION;
                            }
                            /* オプション文字列を取得しつつ次の引数文字列に移動 */
                            count++;
                            pspec->argument_string = argv[count];
                        }
                    } else if (arg_str[2 + long_option_len] == '=') {
                        if (pspec->need_argument != COMMAND_LINE_PARSER_TRUE) {
                            /* '='を含むオプションかもしれない... */
                            continue;
                        }
                        /* 既に取得済みのオプションが指定された */
                        if (pspec->acquired == COMMAND_LINE_PARSER_TRUE) {
                            fprintf(stderr, "%s: Option \"%s\" multiply specified. \n", argv[0], pspec->long_option);
                            return COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED;
                        }
                        /* オプション文字列を取得 */
                        pspec->argument_string = &arg_str[2 + long_option_len + 1];
                    } else {
                        /* より長い文字が指定されている. 他のオプションで一致するかもしれないので読み飛ばす. */
                        continue;
                    }
                    /* 取得済み状態にセット */
                    pspec->acquired = COMMAND_LINE_PARSER_TRUE;
                    break;
                }
            }
            /* オプションが見つからなかった */
            if (spec_no == num_specs) {
                fprintf(stderr, "%s: Unknown long option - \"%s\" \n", argv[0], &arg_str[2]);
                return COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION;
            }
        } else if (arg_str[0] == '-') {
            /* ショートオプション（の連なり） */
            uint32_t str_index;
            for (str_index = 1; arg_str[str_index] != '\0'; str_index++) {
                for (spec_no = 0; spec_no < num_specs; spec_no++) {
                    struct CommandLineParserSpecification* pspec = &clps[spec_no];
                    if (arg_str[str_index] == pspec->short_option) {
                        /* 既に取得済みのオプションが指定された */
                        if (pspec->acquired == COMMAND_LINE_PARSER_TRUE) {
                            fprintf(stderr, "%s: Option \'%c\' multiply specified. \n", argv[0], pspec->short_option);
                            return COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED;
                        }
                        /* 引数ありのオプション */
                        if (pspec->need_argument == COMMAND_LINE_PARSER_TRUE) {
                            /* 引数を取るオプションの場合は、そのまま引数を取りに行く */
                            if (arg_str[str_index + 1] != '\0') {
                                /* 引数を取るに当たり、現在注目しているオプションが末尾である必要がある */
                                fprintf(stderr, "%s: Option \'%c\' needs argument. "
                                        "Please specify tail of short option sequence.\n", argv[0], pspec->short_option);
                                return COMMAND_LINE_PARSER_RESULT_INVAILD_SHORT_OPTION_ARGUMENT;
                            }
                            if ((count + 1) == argc) {
                                /* 終端に達している */
                                fprintf(stderr, "%s: Option \'%c\' needs argument. \n", argv[0], pspec->short_option);
                                return COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION;
                            } else if ((strncmp(argv[count + 1], "--", 2) == 0) || argv[count + 1][0] == '-') {
                                /* 他のオプション指定が入っている */
                                /* （引数として"--", '-'が先頭にくるものは認めない） */
                                fprintf(stderr, "%s: Option \'%c\' needs argument. \n", argv[0], pspec->short_option);
                                return COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION;
                            }
                            /* オプション文字列を取得しつつ次の引数文字列に移動 */
                            count++;
                            pspec->argument_string = argv[count];
                        }
                        /* 取得済み状態にセット */
                        pspec->acquired = COMMAND_LINE_PARSER_TRUE;
                        break;
                    }
                }
                /* オプションが見つからなかった */
                if (spec_no == num_specs) {
                    fprintf(stderr, "%s: Unknown short option - \'%c\' \n", argv[0], arg_str[str_index]);
                    return COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION;
                }
            }
        } else {
            /* オプションでもオプション引数でもない文字列 */
            if (other_string_array == NULL) {
                /* バッファにアクセスできない */
                return COMMAND_LINE_PARSER_RESULT_INVALID_ARGUMENT;
            } else if (other_string_index >= other_string_array_size) {
                /* バッファサイズ不足 */
                fprintf(stderr, "%s: Too many strings specified. \n", argv[0]);
                return COMMAND_LINE_PARSER_RESULT_INSUFFICIENT_OTHER_STRING_ARRAY_SIZE;
            }
            /* 文字列取得 */
            other_string_array[other_string_index] = arg_str;
            other_string_index++;
        }
    }

    return COMMAND_LINE_PARSER_RESULT_OK;
}
