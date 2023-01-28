#ifndef COMMAND_LINE_PARSER_H_INCLDED
#define COMMAND_LINE_PARSER_H_INCLDED

#include <stdint.h>

/* 取得結果 */
typedef enum CommandLineParserResultTag {
    COMMAND_LINE_PARSER_RESULT_OK,                                    /* 正常終了 */
    COMMAND_LINE_PARSER_RESULT_INVALID_ARGUMENT,                      /* 不正な引数 */
    COMMAND_LINE_PARSER_RESULT_INSUFFICIENT_OTHER_STRING_ARRAY_SIZE,  /* その他の文字列が入った配列サイズが足らない */
    COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION,        /* 引数の指定が必須のオプションで引数の指定がない */
    COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION,                        /* 定義にないオプションが指定された */
    COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED,             /* オプションが複数回指定された */
    COMMAND_LINE_PARSER_RESULT_INVALID_SPECIFICATION,                 /* 無効な仕様 */
    COMMAND_LINE_PARSER_RESULT_INVAILD_SHORT_OPTION_ARGUMENT          /* ショートオプションの引数の指定が不適切 */
} CommandLineParserResult;

/* 論理定数 */
typedef enum CommandLineParserBoolTag {
    COMMAND_LINE_PARSER_FALSE = 0,	/* 偽 */
    COMMAND_LINE_PARSER_TRUE		    /* 真 */
} CommandLineParserBool;

/* コマンドラインパーサ仕様 */
/* 補足）コマンドラインパーサ仕様の配列の最後の要素の短いオプションに0, 長いオプションにNULLを指定して下さい */
struct CommandLineParserSpecification {
    char 				          short_option;		  /* [in] 短いオプション文字列        */
    const char* 		      long_option;		  /* [in] 長いオプション文字列        */
    const char* 		      description;		  /* [in] 引数の説明                  */
    CommandLineParserBool	need_argument;		/* [in] オプションに引数は必要か？  */
    const char*				    argument_string;	/* [in,out] 得られた文字列          */
    CommandLineParserBool	acquired;		      /* [out] オプションが指定されたか？ */
};

#ifdef __cplusplus
extern "C" {
#endif

/* 引数説明の印字 */
void CommandLineParser_PrintDescription(
        const struct CommandLineParserSpecification* clps);

/* オプション名からそのオプションが指定されたか取得 */
CommandLineParserBool CommandLineParser_GetOptionAcquired(
        const struct CommandLineParserSpecification* clps, const char* option_name);

/* オプション名からそのオプション引数を取得 */
const char* CommandLineParser_GetArgumentString(
        const struct CommandLineParserSpecification* clps, const char* option_name);

/* 引数のパース */
CommandLineParserResult CommandLineParser_ParseArguments(
        struct CommandLineParserSpecification* clps,
        int32_t argc, const char* const* argv,
        const char** other_string_array, uint32_t other_string_array_size);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_LINE_PARSER_H_INCLDED */
