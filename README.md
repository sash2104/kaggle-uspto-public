This is the 6th Place Solution for the USPTO - Explainable AI for Patent Professionals (Kaggle competition).  
The script released has been refactored after the competition ended. Additionally, due to processes that change randomly at each execution or are cut off by time, it is impossible to completely reproduce the submissions made during the contest. However, since the essential parts have not been changed, almost the same Private Score can be achieved.

- Detailed documentation
  - https://www.kaggle.com/competitions/uspto-explainable-ai/discussion/522202
  - [writeup/README.md](writeup/README.md)
- Submission notebook
  - https://www.kaggle.com/code/sash2104/uspto-6th-place-solution
  - [uspto-6th-place-solution.ipynb](uspto-6th-place-solution.ipynb)

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
# Generates the files (dataset/me/*) used in the submission notebook. It took 2-3 days in my environment.
sh preprocess.sh

# Generate files to be used with the C++ solver.
python script/1_convert_to_id.py -f SETTINGS.json -m solver
```

NOTE:
- Including temporary files, approximately 100GB of disk space is required. All temporary files will be output under dataset/temporary, so please delete them as needed.
- The files corresponding to `dataset/me/*` output by `sh preprocess.sh` are uploaded to https://www.kaggle.com/datasets/sash2104/uspto2024dataset2 . Please use them as needed.

## Solve

```sh
sh solve.sh
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

### Third-Party Code

The following files are third-party code licensed under the MIT License:
- `src/argparse.hpp`
- `find_twoshot/argparse.hpp`

For more details, please refer to the respective files.