import argparse
import json
import polars as pl
import glob
from collections import Counter
from tqdm import tqdm
import os
import re

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

def calc_pubnums(test):
    pubnum_list = []
    query_pubnum_list = []
    for row in test.iter_rows():
        query_pubnum_list.append(row[0])
        pubnum_list.extend(row[1:])
    test_pubnums = pl.DataFrame({"publication_number": pubnum_list}).unique(maintain_order=True)
    query_pubnums = pl.DataFrame({"publication_number": query_pubnum_list}).unique(maintain_order=True)
    query_pubnums = query_pubnums.filter(~pl.col("publication_number").is_in(test_pubnums["publication_number"]))
    potential_negative = pl.scan_parquet(f"{DATADIR}/patent_metadata.parquet").select(["publication_number"]).collect()
    potential_negative = potential_negative.filter(~pl.col("publication_number").is_in(test_pubnums["publication_number"]))
    potential_negative = potential_negative.filter(~pl.col("publication_number").is_in(query_pubnums["publication_number"]))
    pubnums = pl.concat([test_pubnums, query_pubnums, potential_negative])
    n_test = test_pubnums.shape[0]
    n_potential_negative = potential_negative.shape[0]
    n_all = pubnums.shape[0]
    print(n_test, n_potential_negative, n_all)
    print(pubnums.head())
    return test_pubnums, pubnums

def convert_pubnums_to_id(pubnums, save: bool):
    pubnums_with_id = pubnums.with_row_index("id")
    pubnum2id = {}
    for row in pubnums_with_id.rows():
        pubnum2id[row[1]] = row[0]
    if save:
        pubnums_with_id = pubnums_with_id.select(["publication_number", "id"])
        pubnums_with_id.write_csv(f"{OUTDIR}/vocab_pubnum.tsv", separator='\t', include_header=False)
    return pubnum2id

def convert_test_to_id(test, pubnum2id):
    test_id = test.select(
        [pl.col(col).map_elements(lambda x: pubnum2id.get(x, -1), return_dtype=pl.Int64).alias(col) for col in test.columns]
    )
    test_id.write_csv(f"{OUTDIR}/test.tsv", separator='\t', include_header=False)

def convert_nshot_to_id(test_pubnums):
    nshot = pl.read_csv(f"{MY_DATADIR}/nshot.tsv", separator="\t")
    test_nshot = test_pubnums.join(nshot, how="inner", on="publication_number")
    print(len(test_nshot))
    print(test_nshot.head())

    def map_pubnum_to_id(pubnum):
        return pubnum2id.get(pubnum, None)

    test_nshot = test_nshot.with_columns(
        pl.col("publication_number").map_elements(map_pubnum_to_id, return_dtype=pl.Int32).alias("pubid")
    )
    test_nshot = test_nshot.select(["pubid", "tokens"])
    test_nshot.write_csv(f"{OUTDIR}/nshot.tsv", separator="\t", include_header=False)

def create_and_write_vocab(df, key, write_key=None):
    if write_key is None:
        write_key = key
    words = set()
    for row in df.select(key).iter_rows():
        for word in row[0]:
            words.add(word)
    words = list(words)
    words.sort()
    words = pl.DataFrame({key: words})
    words = words.with_row_index("id")
    words = words.select([key, "id"])
    print(len(words))
    words.write_csv(f"{OUTDIR}/vocab_{write_key}.tsv", separator='\t', include_header=False)
    word2id = {}
    for row in words.rows():
        word2id[row[0]] = row[1]
    return word2id

def write_x2y(filepath: str, x2y):
    with open(filepath, "w") as f:
        f.write(f"{len(x2y)}\n")
        for y in x2y:
            f.write(f"{len(y)}")
            if len(y) == 0:
                f.write("\n")
                continue
            ystr = " ".join(map(str, y))
            f.write(f" {ystr}\n")

def calc_y2xs(df, id_x, id_y, vocab_x, vocab_y, write_key_y):
    y2xs = [[] for _ in range(len(vocab_y))]
    for row in df.iter_rows():
        x = row[id_x]
        xid = vocab_x[x]
        for y in row[id_y]:
            yid = vocab_y.get(y)
            if yid is None:
                continue
            y2xs[yid].append(xid)
    file = f"{OUTDIR}/{write_key_y}2pubnum.txt"
    write_x2y(file, y2xs)
    print(file)

def calc_x2ys(df, id_x, id_y, vocab_x, vocab_y, write_key_y):
    x2ys = [[] for _ in range(len(vocab_x))]
    for row in df.iter_rows():
        x = row[id_x]
        xid = vocab_x[x]
        x2ys[xid] = [vocab_y[y] for y in row[id_y]]
    file = f"{OUTDIR}/pubnum2{write_key_y}.txt"
    write_x2y(file, x2ys)  
    print(file)

def convert_cpc_codes_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, test_only: bool):
    df = pubnums.select("publication_number")
    df = df.join(metadata, on="publication_number", how="left")
    ID_PUBNUM = 0
    ID_CPC = 1
    print(df.head())
    if test_only:
        df_test = df.join(test_pubnums, on="publication_number", how="inner")
        cpc2id = create_and_write_vocab(df_test, "cpc_codes", "cpc")
        calc_x2ys(df_test, ID_PUBNUM, ID_CPC, test_pubnum2id, cpc2id, "cpc")
        calc_y2xs(df, ID_PUBNUM, ID_CPC, pubnum2id, cpc2id, "cpc")
    else:
        cpc2id = create_and_write_vocab(df, "cpc_codes", "cpc")
        calc_x2ys(df, ID_PUBNUM, ID_CPC, pubnum2id, cpc2id, "cpc")
        calc_y2xs(df, ID_PUBNUM, ID_CPC, pubnum2id, cpc2id, "cpc")

def main():
    parser = argparse.ArgumentParser(description='')
    parser.add_argument('-f', '--setting-file', type=str, required=True, help='Path to the setting file')
    parser.add_argument('--test-only', action='store_true', help='Save only the information related to test.csv')
    parser.add_argument('--save-nshot', action='store_true', help='If true, convert publication number of nshot.tsv to ID and save it')

    args = parser.parse_args()
    set_variables_from_file(args.setting_file)
    test_only = args.test_only
    save_nshot = args.save_nshot

    os.makedirs(f"{OUTDIR}", exist_ok=True)
    
    test = pl.read_csv(TESTFILE)
    test_pubnums, pubnums = calc_pubnums(test)
    test_pubnum2id = convert_pubnums_to_id(test_pubnums, test_only)
    pubnum2id = convert_pubnums_to_id(pubnums, not test_only)
    convert_test_to_id(test, pubnum2id)
    if save_nshot:
        convert_nshot_to_id(test_pubnum2id)
    convert_cpc_codes_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, test_only)

if __name__ == "__main__":
    main()
