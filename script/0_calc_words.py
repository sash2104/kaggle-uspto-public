import argparse
import json
import polars as pl
import glob
from collections import Counter
from tqdm import tqdm
from joblib import Parallel, delayed
import whoosh
import whoosh.analysis
import os
import re

# Stopwords and numbers have already been excluded in create_vocab.py, so unnecessary processing is reduced.
# analyzer = whoosh.analysis.StandardAnalyzer(stoplist=BRS_STOPWORDS) | NumberFilter()
analyzer = whoosh.analysis.StandardAnalyzer(stoplist=None)

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

def calc_words(key: str, min_freq: int, max_freq: int):
    vocab = pl.read_parquet(f"{MY_DATADIR}/vocab_{key}.parquet")
    allowwords = set(vocab.filter((pl.col("frequency") >= min_freq) & (pl.col("frequency") <= max_freq))["word"].to_list())
    def allow(word):
        return (len(word) >= 2) and word in allowwords
    def process_file(file):
        df = pl.scan_parquet(file)
        df = df.select(["publication_number", key]).collect()
        if len(df) > 0:
            df = df.with_columns(
                df[key].map_elements(
                    lambda x: [word for word in set(token.text for token in analyzer(x)) if allow(word)],
                    return_dtype=pl.List(pl.Utf8)).cast(pl.List(pl.Utf8)).alias(key),
            )
            os.makedirs(f"{DATADIR}/temporary/words_{key}", exist_ok=True)
            filebase = file.split("/")[-1]
            outfile = f"{DATADIR}/temporary/words_{key}/{filebase}"
            df.write_parquet(outfile)

    files = glob.glob(f"{DATADIR}/patent_data/*")
    Parallel(n_jobs=3)(delayed(process_file)(file) for file in tqdm(files))

def merge(key: str, max_freq: int):
    df = pl.read_parquet(glob.glob(f"{DATADIR}/temporary/words_{key}/*.parquet"))
    n = max_freq // 1000
    df.write_parquet(f"{DATADIR}/me/{key}_{n}k.parquet")

def main():
    parser = argparse.ArgumentParser(description='')
    parser.add_argument('-f', '--setting-file', type=str, required=True, help='Path to the setting file')
    parser.add_argument('-t', '--field-type', type=str, required=True, help='title|abstract|claims|description')
    parser.add_argument('-m', '--max-freq', type=int, required=True, help='The maximum frequency of the target words in calc_words')

    args = parser.parse_args()
    set_variables_from_file(args.setting_file)
    
    field = args.field_type
    max_freq = args.max_freq
    calc_words(field, 2, max_freq)
    merge(field, max_freq)

if __name__ == "__main__":
    main()
