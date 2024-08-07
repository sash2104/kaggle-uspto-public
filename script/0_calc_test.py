import argparse
import json
import polars as pl

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

def calc_testfile(seed: int = 1):
    metadata = pl.scan_parquet(f"{DATADIR}/patent_metadata.parquet").collect()
    metadata = metadata.filter(pl.col("publication_date") >= pl.lit("1975-01-01").str.strptime(pl.Date, "%Y-%m-%d"))
    print(len(metadata))
    metadata = metadata.filter(pl.col("cpc_codes") != [])
    metadata = metadata.select(["publication_number"])
    print(len(metadata))

    nn = pl.read_csv(f"{DATADIR}/nearest_neighbors.csv")
    nn_for_test = nn.sample(2500, seed=seed)
    nn_for_test.write_csv(f"{MY_DATADIR}/test.csv")

def main():
    parser = argparse.ArgumentParser(description='')
    parser.add_argument('-f', '--setting-file', type=str, required=True, help='Path to the setting file')

    args = parser.parse_args()
    set_variables_from_file(args.setting_file)

    calc_testfile()

if __name__ == "__main__":
    main()
