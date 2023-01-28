#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/command_line_parser/src/command_line_parser.c"
}

/* ショートオプションの取得テスト */
TEST(CommandLineParserTest, GetShortOptionTest)
{

    /* 簡単な成功例 */
    {
#define INPUT_FILE_NAME "inputfile"
        static const struct CommandLineParserSpecification specs[] = {
            { 'i', NULL, "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', NULL, "output file", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        struct CommandLineParserSpecification get_specs[sizeof(specs) / sizeof(specs[0])];
        const char* test_argv1[] = { "progname", "-i", INPUT_FILE_NAME, "-p" };
        const char* test_argv2[] = { "progname", "-p", "-i", INPUT_FILE_NAME };
        const char* test_argv3[] = { "progname", "-pi", INPUT_FILE_NAME };

        /* パースしてみる */
        memcpy(get_specs, specs, sizeof(get_specs));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OK,
                CommandLineParser_ParseArguments(
                    get_specs,
                    sizeof(test_argv1) / sizeof(test_argv1[0]), test_argv1,
                    NULL, 0));

        /* 正しく取得できたかチェック */
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, get_specs[0].acquired);
        EXPECT_TRUE(get_specs[0].argument_string != NULL);
        if (get_specs[0].argument_string != NULL) {
            EXPECT_EQ(strcmp(INPUT_FILE_NAME, get_specs[0].argument_string), 0);
        }
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, get_specs[1].acquired);

        /* 引数順番を変えたものをパースしてみる */
        memcpy(get_specs, specs, sizeof(get_specs));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OK,
                CommandLineParser_ParseArguments(
                    get_specs,
                    sizeof(test_argv2) / sizeof(test_argv2[0]), test_argv2,
                    NULL, 0));

        /* 正しく取得できたかチェック */
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, get_specs[0].acquired);
        EXPECT_TRUE(get_specs[0].argument_string != NULL);
        if (get_specs[0].argument_string != NULL) {
            EXPECT_EQ(0, strcmp(get_specs[0].argument_string, INPUT_FILE_NAME));
        }
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, get_specs[1].acquired);

        /* ショートオプションの連なりを含むものをパースしてみる */
        memcpy(get_specs, specs, sizeof(get_specs));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OK,
                CommandLineParser_ParseArguments(
                    get_specs,
                    sizeof(test_argv3) / sizeof(test_argv3[0]), test_argv3,
                    NULL, 0));

        /* 正しく取得できたかチェック */
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, get_specs[0].acquired);
        EXPECT_TRUE(get_specs[0].argument_string != NULL);
        if (get_specs[0].argument_string != NULL) {
            EXPECT_EQ(0, strcmp(get_specs[0].argument_string, INPUT_FILE_NAME));
        }
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, get_specs[1].acquired);
#undef INPUT_FILE_NAME
    }

    /* 失敗系 */

    /* 引数が指定されずに末尾に達した */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', NULL, "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "-i" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));
    }

    /* 引数に他のオプションが指定されている */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', NULL, "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv1[] = { "progname", "-i", "-p" };
        const char* test_argv2[] = { "progname", "-i", "--pripara" };

        EXPECT_EQ(
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv1) / sizeof(test_argv1[0]), test_argv1,
                    NULL, 0),
                COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION);

        EXPECT_EQ(
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv2) / sizeof(test_argv2[0]), test_argv2,
                    NULL, 0),
                COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION);
    }

    /* 仕様にないオプションが指定された */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', NULL, "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', NULL, "output file", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "-i", "kiriya aoi", "-p", "-s" };

        EXPECT_EQ(
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0),
                COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION);
    }

    /* 同じオプションが複数回指定されている ケース1 */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', NULL, "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "-i", "kiriya aoi", "-i", "shibuki ran" };

        EXPECT_EQ(
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0),
                COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED);
    }

    /* 同じオプションが複数回指定されている ケース2 */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', NULL, "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', NULL, "aikatsu", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "-p", "-i", "kiriya aoi", "-p" };

        EXPECT_EQ(
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0),
                COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED);
    }

    /* ショートオプションの使い方が正しくない パート1 */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', NULL, "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', NULL, "aikatsu", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "-ip", "filename" };

        EXPECT_EQ(
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0),
                COMMAND_LINE_PARSER_RESULT_INVAILD_SHORT_OPTION_ARGUMENT);
    }

    /* ショートオプションの使い方が正しくない パート2 */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', NULL, "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', NULL, "RL", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "-ip", "filename" };

        EXPECT_EQ(
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0),
                COMMAND_LINE_PARSER_RESULT_INVAILD_SHORT_OPTION_ARGUMENT);
    }

}

/* ロングオプションの取得テスト */
TEST(CommandLineParserTest, GetLongOptionTest)
{

    /* 簡単な成功例 */
    {
#define INPUT_FILE_NAME "inputfile"
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input",  "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'a', "aikatsu", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input", INPUT_FILE_NAME, "--aikatsu" };

        /* パースしてみる */
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OK,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));

        /* 正しく取得できたかチェック */
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, specs[0].acquired);
        EXPECT_TRUE(specs[0].argument_string != NULL);
        if (specs[0].argument_string != NULL) {
            EXPECT_EQ(0, strcmp(specs[0].argument_string, INPUT_FILE_NAME));
        }
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, specs[1].acquired);

#undef INPUT_FILE_NAME
    }

    /* 簡単な成功例 パート2 */
    {
#define INPUT_FILE_NAME "inputfile"
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file",  COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'a', "aikatsu", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input=" INPUT_FILE_NAME, "--aikatsu" };

        /* パースしてみる */
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OK,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));

        /* 正しく取得できたかチェック */
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, specs[0].acquired);
        EXPECT_TRUE(specs[0].argument_string != NULL);
        if (specs[0].argument_string != NULL) {
            EXPECT_EQ(0, strcmp(specs[0].argument_string, INPUT_FILE_NAME));
        }
        EXPECT_EQ(COMMAND_LINE_PARSER_TRUE, specs[1].acquired);

#undef INPUT_FILE_NAME
    }

    /* 失敗系 */

    /* 引数が指定されずに末尾に達した */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));
    }

    /* 引数に他のオプションが指定されている */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv1[] = { "progname", "--input", "-a" };
        const char* test_argv2[] = { "progname", "--input", "--aikatsu" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv1) / sizeof(test_argv1[0]), test_argv1,
                    NULL, 0));

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_NOT_SPECIFY_ARGUMENT_TO_OPTION,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv2) / sizeof(test_argv2[0]), test_argv2,
                    NULL, 0));
    }

    /* 仕様にないオプションが指定された */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', "aikatsu", "aikatsu mode", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input", "kiriya aoi", "--aikatsu", "--unknown" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));
    }

    /* 同じオプションが複数回指定されている ケース1 */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input", "kiriya aoi", "--input", "shibuki ran" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));
    }

    /* 同じオプションが複数回指定されている ケース2 */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input=kiriya aoi", "--input=shibuki ran" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));
    }

    /* 同じオプションが複数回指定されている ケース3 */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input=kiriya aoi", "--input", "shibuki ran" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OPTION_MULTIPLY_SPECIFIED,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));
    }
}

/* 他文字列オプションの取得テスト */
TEST(CommandLineParserTest, GetOtherStringTest)
{

    /* 簡単な成功例 */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "Ichgo Hoshimiya", "Aoi Kiriya", "Ran Shibuki" };
        const char* other_string_array[3];

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OK,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    other_string_array, sizeof(other_string_array) / sizeof(other_string_array[0])));

        /* 順番含め取れたか確認 */
        EXPECT_EQ(0, strcmp(other_string_array[0], test_argv[1]));
        EXPECT_EQ(0, strcmp(other_string_array[1], test_argv[2]));
        EXPECT_EQ(0, strcmp(other_string_array[2], test_argv[3]));
    }

    /* オプションを混ぜてみる */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "Ichgo Hoshimiya", "-i", "inputfile", "Aoi Kiriya", "Ran Shibuki" };
        const char* other_string_array[3];

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_OK,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    other_string_array, sizeof(other_string_array) / sizeof(other_string_array[0])));

        /* 順番含め取れたか確認 */
        EXPECT_EQ(0, strcmp(other_string_array[0], test_argv[1]));
        EXPECT_EQ(0, strcmp(other_string_array[1], test_argv[4]));
        EXPECT_EQ(0, strcmp(other_string_array[2], test_argv[5]));
        EXPECT_EQ(0, strcmp(specs[0].argument_string, "inputfile"));
    }

    /* 失敗系 */

    /* バッファサイズが足らない */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "Ichgo Hoshimiya", "Aoi Kiriya", "Ran Shibuki" };
        const char* other_string_array[2];

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_INSUFFICIENT_OTHER_STRING_ARRAY_SIZE,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    other_string_array, sizeof(other_string_array) / sizeof(other_string_array[0])));
    }
}

/* 様々な文字列設定に対するテスト */
TEST(CommandLineParserTest, ParseVariousStringTest)
{

    /* 失敗系 */

    /* パーサ仕様が不正 ケース1（ショートオプション重複） */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'a', "aikatsu", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'i', NULL, "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input", "inputfile", "--aikatsu" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_INVALID_SPECIFICATION,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));
    }

    /* パーサ仕様が不正 ケース2（ロングオプション重複） */
    {
        struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE,  NULL, COMMAND_LINE_PARSER_FALSE },
            { 'a', "aikatsu", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', "input", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };
        const char* test_argv[] = { "progname", "--input", "inputfile", "--aikatsu" };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_INVALID_SPECIFICATION,
                CommandLineParser_ParseArguments(
                    specs,
                    sizeof(test_argv) / sizeof(test_argv[0]), test_argv,
                    NULL, 0));
    }
}

/* インデックス取得テスト */
TEST(CommandLineParserTest, GetSpecificationIndexTest)
{
    /* 簡単な成功例 */
    {
        uint32_t get_index, spec_no;
        const struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'a', "aikatsu", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', "RL", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, }
        };
        uint32_t num_specs = CommandLineParser_GetNumSpecifications(specs);

        for (spec_no = 0; spec_no < num_specs; spec_no++) {
            char short_option_str[2];
            /* ショートオプションに対してテスト */
            short_option_str[0] = specs[spec_no].short_option;
            short_option_str[1] = '\0';
            EXPECT_EQ(
                    COMMAND_LINE_PARSER_RESULT_OK,
                    CommandLineParser_GetSpecificationIndex(
                        specs, short_option_str, &get_index));
            EXPECT_EQ(spec_no, get_index);
            /* ロングオプションに対してテスト */
            EXPECT_EQ(
                    COMMAND_LINE_PARSER_RESULT_OK,
                    CommandLineParser_GetSpecificationIndex(
                        specs, specs[spec_no].long_option, &get_index));
            EXPECT_EQ(spec_no, get_index);
        }
    }

    /* 失敗系 */

    /* 引数が不正 */
    {
        uint32_t get_index;
        const struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'a', "aikatsu", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', "RL", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, NULL, }
        };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_INVALID_ARGUMENT,
                CommandLineParser_GetSpecificationIndex(
                    NULL, "aikatsu", &get_index));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_INVALID_ARGUMENT,
                CommandLineParser_GetSpecificationIndex(
                    specs, NULL, &get_index));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_INVALID_ARGUMENT,
                CommandLineParser_GetSpecificationIndex(
                    specs, "aikatsu", NULL));
    }

    /* 存在しないオプションのインデックスを問い合わせる */
    {
        uint32_t get_index;
        const struct CommandLineParserSpecification specs[] = {
            { 'i', "input", "input file", COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 'a', "aikatsu", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE,  NULL, COMMAND_LINE_PARSER_FALSE },
            { 'p', "RL", "aikatsu dakega boku no shinri datta...", COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
            { 0, }
        };

        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION,
                CommandLineParser_GetSpecificationIndex(
                    specs, "aikats", &get_index));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION,
                CommandLineParser_GetSpecificationIndex(
                    specs, "naru ayase", &get_index));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION,
                CommandLineParser_GetSpecificationIndex(
                    specs, "rinne ibara", &get_index));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION,
                CommandLineParser_GetSpecificationIndex(
                    specs, "s", &get_index));
        EXPECT_EQ(
                COMMAND_LINE_PARSER_RESULT_UNKNOWN_OPTION,
                CommandLineParser_GetSpecificationIndex(
                    specs, "b", &get_index));
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
