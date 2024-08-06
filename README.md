

## Setup

```sh
# mkdir ~/.kaggle/
# vi ~/.kaggle/kaggle.json

mkdir -p dataset/me
pip3 install -r requirements.txt
kaggle competitions download -c uspto-explainable-ai -p dataset

cd uspto-explainable-ai.zip
unzip uspto-explainable-ai.zip
cd ..


```

```sh
python script/0_calc_test.py -f SETTINGS.local.json

python script/0_calc_vocab.py -f SETTINGS.local.json -t title
python script/0_calc_vocab.py -f SETTINGS.local.json -t abstract
python script/0_calc_vocab.py -f SETTINGS.local.json -t claims
python script/0_calc_vocab.py -f SETTINGS.local.json -t description

python script/0_calc_oneshot.py -f SETTINGS.local.json -t title 
python script/0_calc_oneshot.py -f SETTINGS.local.json -t abstract
python script/0_calc_oneshot.py -f SETTINGS.local.json -t claims
python script/0_calc_oneshot.py -f SETTINGS.local.json -t description

python script/0_calc_words.py -f SETTINGS.local.json -t title -m 400000
python script/0_calc_words.py -f SETTINGS.local.json -t abstract -m 400000
python script/0_calc_words.py -f SETTINGS.local.json -t claims -m 100000
python script/0_calc_words.py -f SETTINGS.local.json -t description -m 10000
```