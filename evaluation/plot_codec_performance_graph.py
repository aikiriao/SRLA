" コーデック評価グラフの作成 "
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
from adjustText import adjust_text

# type3 font 回避(tex使用)
matplotlib.rcParams['text.usetex'] = True
matplotlib.rcParams['text.latex.preamble'] = '\\usepackage{sfmath}'
# フォントサイズ一括設定
matplotlib.rcParams["font.size"] = 12
matplotlib.rcParams['pgf.texsystem'] = 'lualatex'

OTHER_CODEC_LABEL_PREFIXES = ['FLAC', 'WavPack', 'TTA', 'Monkey\'s Audio', 'MPEG4-ALS', 'TAK', 'HALAC V.0.3.8']
COLORLIST = ['crimson', 'g', 'b', 'c', 'm', 'k', 'purple', 'red', 'orange']
CATEGORIES = ['classic', 'genre', 'jazz', 'popular', 'right', 'total']

AVOID_LABEL_LIST = ['FLAC -0', 'WavPack -x4', 'WavPack -h -x4', 'WavPack -hh -x4', 'Monkey\'s Audio -c4000', 'MPEG4-ALS -b', 'MPEG4-ALS -7', 'TAK -p0e', 'TAK -p0', 'TAK -p1e', 'TAK -p1', 'TAK -p2e', 'TAK -p2', 'TAK -p3e', 'TAK -p3','TAK -p4e', 'TAK -p4', 'SRLA -m 0', 'HALAC V.0.3.8 -mt=1 -ufast']

def _is_avoid_label(label, include_match=False):
    if include_match is True:
        for content in AVOID_LABEL_LIST:
            if label.startswith(content):
                return True
    else:
        for content in AVOID_LABEL_LIST:
            if label == content:
                return True
    return False

if __name__ == "__main__":
    other_codecs_df = pd.read_csv('codec_comparison_summery_other_codecs.csv', index_col=0)
    srla_codecs_df = pd.read_csv('codec_comparison_summery.csv', index_col=0)
    avx2_srla_codecs_df = pd.read_csv('codec_comparison_summery_avx2.csv', index_col=0)

    for block_size in [2048, 4096, 8192]:
        # デコード速度 v.s. 圧縮率グラフ
        for category in CATEGORIES:
            texts = []
            plt.cla()
            plt.figure(figsize=(8, 6))
            # 他コーデック
            for inx, cprefix in enumerate(OTHER_CODEC_LABEL_PREFIXES):
                line = [[], []]
                for label in other_codecs_df.keys():
                    if _is_avoid_label(label) is True:
                        continue
                    if label.startswith(cprefix):
                        decode_time = other_codecs_df.at[f'{category} mean decode time', label]
                        compress_rate = other_codecs_df.at[f'{category} mean compression rate', label]
                        texts.append(plt.text(decode_time, compress_rate, label[len(cprefix):], size=10))
                        line[0].append(decode_time)
                        line[1].append(compress_rate)
                plt.plot(line[0], line[1], color=COLORLIST[inx], label=cprefix, marker='o')
            # SRLA
            for div_index, div in enumerate([0, 2]):
                line = [[], []]
                for label in srla_codecs_df.keys():
                    if label.startswith('SRLA'):
                        if not str(block_size) in label or not f'-V {div}' in label or '-P' in label or _is_avoid_label(label, True):
                            continue
                        option_prefix = label[len('SRLA'):label.index('V') - 1]
                        decode_time = float(srla_codecs_df.at[f'{category} mean decode time', label])
                        compress_rate = float(srla_codecs_df.at[f'{category} mean compression rate', label])
                        texts.append(plt.text(decode_time, compress_rate, option_prefix, size=10))
                        line[0].append(decode_time)
                        line[1].append(compress_rate)
                plt.plot(line[0], line[1], color=COLORLIST[len(OTHER_CODEC_LABEL_PREFIXES) + div_index], label=f'SRLA V={div}', marker='^')
            for div_index, div in enumerate([0]):
                line = [[], []]
                for label in srla_codecs_df.keys():
                    if label.startswith('SRLA'):
                        if not str(block_size) in label or not f'-V {div}' in label or not '-P 3' in label or _is_avoid_label(label, True):
                            continue
                        option_prefix = label[len('SRLA'):label.index('V') - 1]
                        decode_time = float(srla_codecs_df.at[f'{category} mean decode time', label])
                        compress_rate = float(srla_codecs_df.at[f'{category} mean compression rate', label])
                        texts.append(plt.text(decode_time, compress_rate, option_prefix, size=10))
                        line[0].append(decode_time)
                        line[1].append(compress_rate)
                plt.plot(line[0], line[1], color=COLORLIST[len(OTHER_CODEC_LABEL_PREFIXES) + div_index], label=f'SRLA V={div} -P 3', marker='^', linestyle=':')
            # AVX2 SRLA
            for div_index, div in enumerate([0, 2]):
                line = [[], []]
                for label in avx2_srla_codecs_df.keys():
                    if label.startswith('SRLA'):
                        if not str(block_size) in label or not f'-V {div}' in label or '-P' in label or _is_avoid_label(label, True):
                            continue
                        option_prefix = label[len('SRLA'):label.index('V') - 1]
                        decode_time = float(avx2_srla_codecs_df.at[f'{category} mean decode time', label])
                        compress_rate = float(avx2_srla_codecs_df.at[f'{category} mean compression rate', label])
                        texts.append(plt.text(decode_time, compress_rate, option_prefix, size=10))
                        line[0].append(decode_time)
                        line[1].append(compress_rate)
                plt.plot(line[0], line[1], color=COLORLIST[len(OTHER_CODEC_LABEL_PREFIXES) + div_index], label=f'AVX2 SRLA V={div}', marker='^', linestyle='--')

            adjust_text(texts)
            plt.title(f'Decoding speed v.s. compression rate for {category} blocksize:{block_size}')
            plt.xlabel('Average decoding speed (\%)')
            plt.ylabel('Average compression rate (\%)')
            plt.legend(ncols=2)
            plt.grid()
            if category == 'total':
                plt.ylim(ymin=53.5)
            plt.tight_layout()
            plt.savefig(f'decodespeed_vs_compressionrate_{block_size}_{category}.png')
            plt.close()

        # エンコード速度 v.s. 圧縮率グラフ
        for category in CATEGORIES:
            texts = []
            plt.cla()
            plt.figure(figsize=(8, 6))
            # 他コーデック
            for inx, cprefix in enumerate(OTHER_CODEC_LABEL_PREFIXES):
                line = [[], []]
                for label in other_codecs_df.keys():
                    if _is_avoid_label(label) is True:
                        continue
                    if label.startswith(cprefix):
                        encode_time = other_codecs_df.at[f'{category} mean encode time', label]
                        compress_rate = other_codecs_df.at[f'{category} mean compression rate', label]
                        texts.append(plt.text(encode_time, compress_rate, label[len(cprefix):], size=10))
                        line[0].append(encode_time)
                        line[1].append(compress_rate)
                plt.plot(line[0], line[1], color=COLORLIST[inx], label=cprefix, marker='o')
            # SRLA
            for div_index, div in enumerate([0, 2]):
                line = [[], []]
                for label in srla_codecs_df.keys():
                    if label.startswith('SRLA'):
                        if not str(block_size) in label or not f'-V {div}' in label or '-P 3' in label or _is_avoid_label(label, True):
                            continue
                        option_prefix = label[len('SRLA'):label.index('V') - 1]
                        encode_time = srla_codecs_df.at[f'{category} mean encode time', label]
                        compress_rate = srla_codecs_df.at[f'{category} mean compression rate', label]
                        texts.append(plt.text(encode_time, compress_rate, option_prefix, size=10))
                        line[0].append(encode_time)
                        line[1].append(compress_rate)
                plt.plot(line[0], line[1], color=COLORLIST[len(OTHER_CODEC_LABEL_PREFIXES) + div_index], label=f'SRLA V={div}', marker='^')
            for div_index, div in enumerate([0]):
                line = [[], []]
                for label in srla_codecs_df.keys():
                    if label.startswith('SRLA'):
                        if not str(block_size) in label or not f'-V {div}' in label or not '-P 3' in label or _is_avoid_label(label, True):
                            continue
                        option_prefix = label[len('SRLA'):label.index('V') - 1]
                        decode_time = float(srla_codecs_df.at[f'{category} mean encode time', label])
                        compress_rate = float(srla_codecs_df.at[f'{category} mean compression rate', label])
                        texts.append(plt.text(decode_time, compress_rate, option_prefix, size=10))
                        line[0].append(decode_time)
                        line[1].append(compress_rate)
                plt.plot(line[0], line[1], color=COLORLIST[len(OTHER_CODEC_LABEL_PREFIXES) + div_index], label=f'SRLA V={div} -P=3', marker='^', linestyle=':')

            adjust_text(texts)
            plt.title(f'Encoding speed v.s. compression rate for {category} blocksize:{block_size}')
            plt.xlabel('Average encoding speed (\%)')
            plt.ylabel('Average compression rate (\%)')
            plt.legend(ncols=2)
            plt.grid()
            plt.tight_layout()
            plt.savefig(f'encodespeed_vs_compressionrate_{block_size}_{category}.png')
            plt.close()
