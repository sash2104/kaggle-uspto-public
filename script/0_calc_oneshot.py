import argparse
import json
import polars as pl
import glob
from collections import Counter
from tqdm import tqdm
from multiprocessing import Pool, Manager
import polars as pl
import os
import whoosh
import whoosh.analysis
import re

NUMBER_REGEX = re.compile(r'^(\d+|\d{1,3}(,\d{3})*)(\.\d+)?$')
class NumberFilter(whoosh.analysis.Filter):
    def __call__(self, tokens):
        for t in tokens:
            if not NUMBER_REGEX.match(t.text):
                yield t

analyzer = whoosh.analysis.StandardAnalyzer(stoplist=None) | NumberFilter()

def calc_oneshot(key: str):
    df = pl.read_parquet(f"{MY_DATADIR}/vocab_{key}.parquet")
    df = df.filter(pl.col('frequency') == 1)
    oneshot = set(df["word"].to_list())
    print(len(oneshot))

    def extract_oneshot(words):
        for word in words:
            if word in oneshot:
                return word
        return None

    def process_file(file):
        df = pl.scan_parquet(file)
        df = df.select(["publication_number", key]).collect()
        df = (df.with_columns(words=df[key].map_elements(lambda x: list(set(token.text for token in analyzer(x))), return_dtype=pl.List(pl.Utf8)).cast(pl.List(pl.Utf8))))
        df = df.select(["publication_number", "words"])
        before = df.shape[0]
        df = df.with_columns(
            pl.col('words').map_elements(extract_oneshot, return_dtype=pl.Utf8).alias('oneshot_word').cast(pl.Utf8)
        )

        df = df.filter(pl.col('oneshot_word').is_not_null())
        after = df.shape[0]
        df = df.select(["publication_number", "oneshot_word"])
        os.makedirs(f"{DATADIR}/temporary/oneshot_{key}", exist_ok=True)
        filebase = file.split("/")[-1]
        outfile = f"{DATADIR}/temporary/oneshot_{key}/{filebase}.tsv"
        df.write_csv(outfile, separator="\t", include_header=False)
        # print(outfile, before, after)
        return before, after,

    files = glob.glob(f"{DATADIR}/patent_data/*.parquet")
    sum_before = 0
    sum_after = 0
    for file in tqdm(files):
        before, after = process_file(file)
        sum_before += before
        sum_after += after
        rate = sum_after/sum_before
        # print(file, before, after, sum_before, sum_after, rate)

def merge_oneshot(key: str):
    df = pl.read_csv(f"{DATADIR}/temporary/oneshot_{key}/*.tsv",
                     separator='\t', has_header=False,
                     new_columns=["publication_number", "tokens"])
    df.write_csv(f"{DATADIR}/temporary/oneshot_{key}.tsv", separator='\t')

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

def main():
    parser = argparse.ArgumentParser(description='')
    parser.add_argument('-f', '--setting-file', type=str, required=True, help='Path to the setting file')
    parser.add_argument('-t', '--field-type', type=str, required=True, help='title|abstract|claims|description')

    args = parser.parse_args()
    set_variables_from_file(args.setting_file)
    
    field = args.field_type
    calc_oneshot(field)
    merge_oneshot(field)

if __name__ == "__main__":
    main()
