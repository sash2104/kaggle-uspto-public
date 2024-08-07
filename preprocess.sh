#!/usr/bin/env sh

python script/0_calc_test.py -f SETTINGS.json

# It took about 10 hours in my execution environment.
python script/0_calc_vocab.py -f SETTINGS.json -t title
python script/0_calc_vocab.py -f SETTINGS.json -t abstract
python script/0_calc_vocab.py -f SETTINGS.json -t claims
python script/0_calc_vocab.py -f SETTINGS.json -t description

# It took about 10 hours in my execution environment.
python script/0_calc_words.py -f SETTINGS.json -t title -m 400000
python script/0_calc_words.py -f SETTINGS.json -t abstract -m 400000
python script/0_calc_words.py -f SETTINGS.json -t claims -m 100000
python script/0_calc_words.py -f SETTINGS.json -t description -m 10000

# --- Generate n-shot subqueries.
## Generate oneshot subqueries.
## It took about 10 hours in my execution environment.
python script/0_calc_oneshot.py -f SETTINGS.json -t title 
python script/0_calc_oneshot.py -f SETTINGS.json -t abstract
python script/0_calc_oneshot.py -f SETTINGS.json -t claims
python script/0_calc_oneshot.py -f SETTINGS.json -t description

## Generate twoshot subqueries.
## It took about 10 hours in my execution environment.
python script/1_convert_to_id.py -f SETTINGS.json -m find_twoshot
datadir=$(cat SETTINGS.json | jq -r .DATADIR)
find_twoshot_indir=$(cat SETTINGS.json | jq -r .FIND_TWOSHOT_INDIR)
g++ -std=c++17 -O3 find_twoshot/main.cpp -o find_twoshot/run
find_twoshot/run --datadir "${find_twoshot_indir}/" --field cpc --field title --field abstract --field claims --field description > ${datadir}/temporary/twoshot.tsv
# ---

## Generate files to be used with the C++ solver.
python script/1_convert_to_id.py -f SETTINGS.json -m solver