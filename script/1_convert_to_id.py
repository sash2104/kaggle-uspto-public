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

def convert_pubnums_to_id(pubnums, save: bool, outdir: str):
    pubnums_with_id = pubnums.with_row_index("id")
    pubnum2id = {}
    for row in pubnums_with_id.rows():
        pubnum2id[row[1]] = row[0]
    if save:
        pubnums_with_id = pubnums_with_id.select(["publication_number", "id"])
        pubnums_with_id.write_csv(f"{outdir}/vocab_pubnum.tsv", separator='\t', include_header=False)
    return pubnum2id

def convert_test_to_id(test, pubnum2id, outdir: str):
    test_id = test.select(
        [pl.col(col).map_elements(lambda x: pubnum2id.get(x, -1), return_dtype=pl.Int64).alias(col) for col in test.columns]
    )
    test_id.write_csv(f"{outdir}/test.tsv", separator='\t', include_header=False)

def convert_nshot_to_id(test_pubnums, outdir: str):
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
    test_nshot.write_csv(f"{outdir}/nshot.tsv", separator="\t", include_header=False)

def create_and_write_vocab(df, key, outdir, write_key=None):
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
    words.write_csv(f"{outdir}/vocab_{write_key}.tsv", separator='\t', include_header=False)
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

def calc_y2xs(df, id_x, id_y, vocab_x, vocab_y, write_key_y, outdir: str):
    y2xs = [[] for _ in range(len(vocab_y))]
    for row in df.iter_rows():
        x = row[id_x]
        xid = vocab_x[x]
        for y in row[id_y]:
            yid = vocab_y.get(y)
            if yid is None:
                continue
            y2xs[yid].append(xid)
    file = f"{outdir}/{write_key_y}2pubnum.txt"
    write_x2y(file, y2xs)
    print(file)

def calc_x2ys(df, id_x, id_y, vocab_x, vocab_y, write_key_y, outdir: str):
    x2ys = [[] for _ in range(len(vocab_x))]
    for row in df.iter_rows():
        x = row[id_x]
        xid = vocab_x[x]
        x2ys[xid] = [vocab_y[y] for y in row[id_y]]
    file = f"{outdir}/pubnum2{write_key_y}.txt"
    write_x2y(file, x2ys)  
    print(file)

def convert_cpc_codes_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, test_only: bool, outdir: str):
    df = pubnums.select("publication_number")
    metadata = pl.scan_parquet(f"{DATADIR}/patent_metadata.parquet").select(["publication_number", "cpc_codes"]).collect()
    df = df.join(metadata, on="publication_number", how="left")
    ID_PUBNUM = 0
    ID_CPC = 1
    print(df.head())
    if test_only:
        df_test = df.join(test_pubnums, on="publication_number", how="inner")
        cpc2id = create_and_write_vocab(df_test, "cpc_codes", outdir, "cpc")
        calc_x2ys(df_test, ID_PUBNUM, ID_CPC, test_pubnum2id, cpc2id, "cpc")
        calc_y2xs(df, ID_PUBNUM, ID_CPC, pubnum2id, cpc2id, "cpc")
    else:
        cpc2id = create_and_write_vocab(df, "cpc_codes", outdir, "cpc")
        calc_x2ys(df, ID_PUBNUM, ID_CPC, pubnum2id, cpc2id, "cpc")
        calc_y2xs(df, ID_PUBNUM, ID_CPC, pubnum2id, cpc2id, "cpc")

def convert_x_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, field: str, max_freq: int, test_only: bool, outdir: str):
    ID_PUBNUM = 0
    ID_X = 1
    freq = f"{max_freq//1000}k"
    df_x = pl.scan_parquet(f"{MY_DATADIR}/{field}_{freq}.parquet").collect()
    if test_only:
        df_x_test = df_x.join(test_pubnums, on="publication_number", how="inner")
        print(field, len(df_x_test))
        print(df_x_test.head())
        x2id = create_and_write_vocab(df_x_test, field, outdir)
        calc_x2ys(df_x_test, ID_PUBNUM, ID_X, test_pubnum2id, x2id, field, outdir)
        calc_y2xs(df_x, ID_PUBNUM, ID_X, pubnum2id, x2id, field, outdir)
    else:
        print(field, len(df_x))
        print(df_x.head())
        x2id = create_and_write_vocab(df_x, field, outdir)
        calc_x2ys(df_x, ID_PUBNUM, ID_X, pubnum2id, x2id, field, outdir)
        calc_y2xs(df_x, ID_PUBNUM, ID_X, pubnum2id, x2id, field, outdir)

def main():
    parser = argparse.ArgumentParser(description='')
    parser.add_argument('-f', '--setting-file', type=str, required=True, help='Path to the setting file')
    parser.add_argument('-m', '--mode', type=str, required=True, help='find_twoshot|solver')

    args = parser.parse_args()
    set_variables_from_file(args.setting_file)

    if args.mode == "find_twoshot":
        test_only = False
        save_nshot = False
        outdir = FIND_TWOSHOT_INDIR
    elif args.mode == "solver":
        test_only = True
        save_nshot = True
        outdir = OUTDIR
    else:
        print("--mode should be one of (find_twoshot|solver)")
        assert False
    os.makedirs(f"{outdir}", exist_ok=True)
    
    test = pl.read_csv(TESTFILE)
    test_pubnums, pubnums = calc_pubnums(test)
    test_pubnum2id = convert_pubnums_to_id(test_pubnums, test_only, outdir)
    pubnum2id = convert_pubnums_to_id(pubnums, not test_only, outdir)
    convert_test_to_id(test, pubnum2id, outdir)
    if save_nshot:
        convert_nshot_to_id(test_pubnum2id, outdir)
    convert_cpc_codes_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, test_only)
    convert_x_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, "title", 400000, test_only, outdir)
    convert_x_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, "abstract", 400000, test_only, outdir)
    convert_x_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, "claims", 100000, test_only, outdir)
    convert_x_to_id(pubnums, test_pubnums, pubnum2id, test_pubnum2id, "description", 10000, test_only, outdir)

if __name__ == "__main__":
    main()
