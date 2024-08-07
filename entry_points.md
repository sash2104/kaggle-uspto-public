1. `sh preprocess.sh`
- Generates the files (dataset/me/*) used in the submission notebook. It took 2-3 days in my environment.

2. `python script/1_convert_to_id.py -f SETTINGS.json -m solver`
- Generate files to be used with the C++ solver.

3. `sh solve.sh`
- Generate queries using the C++ solver and create submission.csv.