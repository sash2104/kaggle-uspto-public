This is the 6th Place Solution for the USPTO - Explainable AI for Patent Professionals (Kaggle competition).  
The script released has been refactored after the competition ended. Additionally, due to processes that change randomly at each execution or are cut off by time, it is impossible to completely reproduce the submissions made during the contest. However, since the essential parts have not been changed, almost the same Private Score can be achieved.

- Detailed documentation
  - https://www.kaggle.com/competitions/uspto-explainable-ai/discussion/522202
  - TODO: link
- Submission notebook
  - https://www.kaggle.com/code/sash2104/uspto-6th-place-solution
  - TODO: link

## Hardware
Host:
- OS: macOS Sonoma 14.3.1
- Model Name: MacBook Pro
- Chip: Apple M1 Pro
- Total Number of Cores: 10 (8 performance and 2 efficiency)
- Memory: 32 GB

Docker Container:
- OS: Ubuntu 22.04.2 LTS

## Software
- Docker version 20.10.12, build e91ed57
- Python 3.10.12
- gcc version 11.4.0 (Ubuntu 11.4.0-1ubuntu1~22.04)

[Dockerfile](.devcontainer/Dockerfile)

## Data Setup
(assumes the [Kaggle API](https://github.com/Kaggle/kaggle-api) is installed)

```sh
mkdir -p dataset/me
kaggle competitions download -c uspto-explainable-ai -p dataset

cd uspto-explainable-ai.zip
unzip uspto-explainable-ai.zip
cd ..
```

## Data Processing
```sh
python script/0_calc_test.py -f SETTINGS.local.json

# It took about 10 hours in my execution environment.
python script/0_calc_vocab.py -f SETTINGS.local.json -t title
python script/0_calc_vocab.py -f SETTINGS.local.json -t abstract
python script/0_calc_vocab.py -f SETTINGS.local.json -t claims
python script/0_calc_vocab.py -f SETTINGS.local.json -t description

# It took about 10 hours in my execution environment.
python script/0_calc_oneshot.py -f SETTINGS.local.json -t title 
python script/0_calc_oneshot.py -f SETTINGS.local.json -t abstract
python script/0_calc_oneshot.py -f SETTINGS.local.json -t claims
python script/0_calc_oneshot.py -f SETTINGS.local.json -t description

# It took about 10 hours in my execution environment.
python script/0_calc_words.py -f SETTINGS.local.json -t title -m 400000
python script/0_calc_words.py -f SETTINGS.local.json -t abstract -m 400000
python script/0_calc_words.py -f SETTINGS.local.json -t claims -m 100000
python script/0_calc_words.py -f SETTINGS.local.json -t description -m 10000
```

NOTE:
- 一時ファイルを含めて、100GB程度のディスク容量が必要です。一時ファイルはすべてdataset/temporary以下に出力されるので、必要に応じて削除してください

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

### Third-Party Code

The following files are third-party code licensed under the MIT License:
- `src/argparse.hpp`
- `find_twoshot/argparse.hpp`

For more details, please refer to the respective files.