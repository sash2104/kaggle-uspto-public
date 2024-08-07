#!/usr/bin/env sh

outdir=$(cat SETTINGS.json | jq -r .OUTDIR)
g++ -std=c++17 -O3 src/main.cpp -o src/run
# src/run --datadir "${outdir}/" --field cpc --field title --field abstract --field claims --field description > ${outdir}/result.txt
src/run --datadir "${outdir}/" --field cpc --field title --field abstract --field claims > ${outdir}/result.txt

python script/submit.py -f SETTINGS.json

head -n 11 ${outdir}/submission.csv