import argparse
import json
import polars as pl
import os

def generate_submit_file():
    with open(f"{OUTDIR}/result.txt") as f:
        queries = []
        for line in f:
            queries.append(line.strip())
    test = pl.read_csv(TESTFILE)
    assert len(queries) == len(test)
    with open(f"{OUTDIR}/submission.csv", "w") as f:
        f.write("publication_number,query\n")
        for i in range(len(queries)):
            pub = test["publication_number"][i]
            f.write(f"{pub},{queries[i]}\n")

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

    args = parser.parse_args()
    set_variables_from_file(args.setting_file)
    
    generate_submit_file()

if __name__ == "__main__":
    main()
