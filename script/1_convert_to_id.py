import argparse
import json
import polars as pl
import glob
from collections import Counter
from tqdm import tqdm
from multiprocessing import Pool, Manager
import whoosh
import whoosh.analysis
import os
import re

BRS_STOPWORDS = ['an', 'are', 'by', 'for', 'if', 'into', 'is', 'no', 'not', 'of', 'on', 'such',
    'that', 'the', 'their', 'then', 'there', 'these', 'they', 'this', 'to', 'was', 'will']
NUMBER_REGEX = re.compile(r'^(\d+|\d{1,3}(,\d{3})*)(\.\d+)?$')

class NumberFilter(whoosh.analysis.Filter):
    def __call__(self, tokens):
        for t in tokens:
            if not NUMBER_REGEX.match(t.text):
                yield t

analyzer = whoosh.analysis.StandardAnalyzer(stoplist=BRS_STOPWORDS) | NumberFilter()

def set_variables_from_file(setting_file):
    with open(setting_file, 'r') as file:
        config = json.load(file)
    
    for key, value in config.items():
        globals()[key] = value

    print(f"{DATADIR=}")
    print(f"{MY_DATADIR=}")
    print(f"{MY_SCRIPTDIR=}")
    print(f"{OUTDIR=}")
    print(f"{TESTFILE=}")

def calc_vocab(key: str):
    def process_file(file):
        df = pl.scan_parquet(file)
        df = df.select([key]).collect()
        df = (df.select(words=df[key].map_elements(lambda x: list(set(token.text for token in analyzer(x))), return_dtype=pl.List(pl.Utf8)).cast(pl.List(pl.Utf8)))
            .explode("words")
            .to_series()
            .value_counts()
            .filter(pl.col("words").str.len_bytes() > 0) 
            )
        os.makedirs(f"{DATADIR}/temporary/vocab_{key}", exist_ok=True)
        filebase = file.split("/")[-1]
        outfile = f"{DATADIR}/temporary/vocab_{key}/{filebase}.tsv"
        df.write_csv(outfile, separator="\t", include_header=False)

    files = glob.glob(f"{DATADIR}/patent_data/*")
    for file in tqdm(files):
        process_file(file)

def merge_vocab(key: str):
    vocab = Counter()
    files = glob.glob(f"{DATADIR}/temporary/vocab_{key}/*")
    for file in tqdm(files):
        with open(file) as f:
            for line in f:
                data = line.strip().split("\t")
                if '*' in data[0] or '$' in data[0]:
                    # Exclude words that contain the wildcard character
                    continue
                vocab[data[0]] += int(data[1])

    # write
    df_vocab = pl.DataFrame({
        "word": list(vocab.keys()),
        "frequency": list(vocab.values())
    })

    df_vocab = df_vocab.sort(by="frequency", descending=True)

    df_vocab = df_vocab.with_columns(
        pl.arange(0, df_vocab.height).alias("id")
    )

    df_vocab.write_parquet(f'{MY_DATADIR}/vocab_{key}.parquet')
    return vocab

def main():
    parser = argparse.ArgumentParser(description='Process a setting file path.')
    parser.add_argument('-f', '--setting-file', type=str, required=True, help='Path to the setting file')
    parser.add_argument('-t', '--field-type', type=str, required=True, help='title|abstract|claims|description')

    args = parser.parse_args()
    set_variables_from_file(args.setting_file)
    
    field = args.field_type
    calc_vocab(field)
    merge_vocab(field)

if __name__ == "__main__":
    main()
