import argparse
import json
import polars as pl
import os

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

def merge_nshot():
    nshot = {}
    key2field = {
        "title": "ti",
        "abstract": "ab",
        "claims": "clm",
        "description": "detd",
    }
    for key in ["title", "abstract", "claims", "description"]:
        filepath = f"{DATADIR}/temporary/oneshot_{key}.tsv"
        oneshot = pl.read_csv(filepath, separator="\t")
        for row in oneshot.iter_rows():
            pubnum = row[0]
            token = f"{key2field[key]}:{row[1]}"
            if pubnum in nshot:
                continue
            nshot[pubnum] = token
        print(key, len(nshot))
    twoshot_filepath = f"{DATADIR}/temporary/twoshot.tsv"
    twoshot = pl.read_csv(f"{DATADIR}/temporary/twoshot.tsv", separator="\t", new_columns=["publication_number", "tokens"])
    for row in twoshot.iter_rows():
        pubnum = row[0]
        token = row[1]
        if pubnum in nshot:
            continue
        nshot[pubnum] = token
    print(len(nshot))
    
    df_nshot = pl.DataFrame({
        "publication_number": list(nshot.keys()),
        "tokens": list(nshot.values())
    })
    df_nshot.write_csv(f"{MY_DATADIR}/nshot.tsv", separator="\t")

def main():
    parser = argparse.ArgumentParser(description='')
    parser.add_argument('-f', '--setting-file', type=str, required=True, help='Path to the setting file')

    args = parser.parse_args()
    set_variables_from_file(args.setting_file)

    merge_nshot()

if __name__ == "__main__":
    main()
