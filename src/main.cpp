#ifndef LOCAL
#pragma GCC target("avx2")
#pragma GCC optimize("O3")
#pragma GCC optimize("unroll-loops")
#endif

#include "argparse.hpp"
#include "util.hpp"

#include <fstream>
#include <memory>
#include <queue>

#ifdef LOCAL
std::string DATADIR = "/workspaces/uspto/output/";
#else
std::string DATADIR = "/kaggle/working/";
#endif

static constexpr int NONE = -1;

static constexpr int FIELD_TITLE = 0;
static constexpr int FIELD_ABSTRACT = 1;
static constexpr int FIELD_CLAIMS = 2;
static constexpr int FIELD_DESCRIPTION = 3;
static constexpr int FIELD_CPC_CODES = 4;

static constexpr int OPERATOR_OR = 0;
static constexpr int OPERATOR_AND = 1;
static constexpr int OPERAND = 2;

static constexpr int MAX_TOKEN = 50;

// The ID with the largest value among the test index. Used to optimize the calculation of `intersect`.
int LAST_TEST_ID = -1;

// static constexpr int N_PUBNUM = 13307751;
int N_PUBNUM = -1;

set<int> active_testids_;
array<string,5> x2field_ = {"ti", "ab", "clm", "detd", "cpc"};
map<string,int> field2x_ = {
  {"ti", FIELD_TITLE},
  {"title", FIELD_TITLE},
  {"ab", FIELD_ABSTRACT},
  {"abstract", FIELD_ABSTRACT},
  {"clm", FIELD_CLAIMS},
  {"claims", FIELD_CLAIMS},
  {"detd", FIELD_DESCRIPTION},
  {"description", FIELD_DESCRIPTION},
  {"cpc", FIELD_CPC_CODES},
  {"cpc_codes", FIELD_CPC_CODES},
};
array<int, 5> field_order_; // Used for pruning branches in DFS (Depth-First Search).

struct Param {
  double dfs_timeout_sec = 0.20;
  bool log_level_info = false;
  int beam_width = 100;
  int max_depth = 3;
  double sampling_ratio = 0.2;
  string solver = "multi";
  vector<int> active_fields;
  Param() {
  }
  friend std::ostream& operator<<(std::ostream& os, const Param &p) {
    os << "max_depth=" << p.max_depth;
    os << ",sampling_ratio=" << p.sampling_ratio;
    os << ",beam_width=" << p.beam_width;
    os << ",timeout=" << p.dfs_timeout_sec;
    os << ",solver=" << p.solver;
    os << ",active_fields=";
    for (int f: p.active_fields) {
      os << x2field_[f] << "|";
    }
    return os;
  }

  void init() {
    if (auto env = std::getenv("P_LOG_LEVEL_INFO")) {
      log_level_info = true;
    }
  }
};

Param param_;

std::unordered_map<int, std::string> nshot_;

// https://stackoverflow.com/questions/20511347/a-good-hash-function-for-a-vector/72073933#72073933
struct VectorHash {
    std::size_t operator()(const std::vector<int>& vec) const {
        std::size_t seed = vec.size();
        for (auto x : vec) {
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

using vec_hash_t = std::unordered_map<std::vector<int>, int, VectorHash>;
using vec_hash_pii_t = std::unordered_map<std::vector<int>, pair<int, int>, VectorHash>;

struct Factorial {
  vector<double> v;
  Factorial(int n) : v(n) {
    v[0] = 1.0;
    for (int i = 1; i <= n; ++i) {
      v[i] = v[i-1] * i;
    }
  }
  double& operator[](size_t i) {
    return v[i];
  }
  const double& operator[](size_t i) const {
    return v[i];
  }
};

Factorial fact_(100000);

double nCk(int n, int k) {
  return fact_[n] / (fact_[k] * fact_[n - k]);
}

//  calculate the probability mass function (PMF) of a binomial distribution
double binomial_pmf(int n, double p, int k) {
  return nCk(n, k) * std::pow(p, k) * std::pow(1 - p, n - k);
}

struct pair_hash {
  std::size_t operator() (const std::pair<int, int>& pair) const {
    return pair.second * 51 + pair.first;
  }
};

struct Scorer {
  unordered_map<pair<int, int>, double, pair_hash> memo1, memo2;
  double ratio;
  Scorer(): ratio(1.0) {}
  Scorer(double ratio_): ratio(ratio_) {}
  double evaluate(int n_positive, int n_all) {
    // D1(n_positive, n_all, ratio);
    return estimateAP50(n_positive, n_all, ratio);
  };
  double evaluate(int n_positive, int n_all, int n_token) {
    // D1(n_positive, n_all, ratio);
    // return n_positive-n_token*0.1-0.0001*n_all;
    // return n_positive-n_token*0.0001-0.02*(n_all-n_positive);
    return estimateAP50(n_positive, n_all, ratio);
  };

  double estimateAP50(int n_positive, int n_all) {
    // score1(n, m) := estimateAP50(n_positive, n_all-n_positive)
    pair<int, int> p = {n_positive, n_all};
    if (memo1.count(p)) return memo1[p];
    int n_trial = 100000;
    if (n_positive == n_all) n_trial = 1;
    vector<int> v(n_all);
    REP(i,n_positive) v[i] = 1;
    double sum_average_precision = 0;
    REP(trial,n_trial) {
      double sum_precision = 0;
      rough_shuffle(v);
      int n_found = 0;
      REP(i,50) {
        if (i < v.size() && v[i]) {
          ++n_found;
          // this is how it probably should be
          // double precision = (double)n_found / (i+1);
          // sum_precision += precision;
        }
        // this is the line that is probably incorrect for competition LB
        double precision = (double)n_found / (i+1);
        sum_precision += precision;
      }
      double average_precision = sum_precision / 50;
      sum_average_precision += average_precision;
      // D1(n_positive, n_all, average_precision);
    }
    memo1[p] = sum_average_precision/n_trial;
    return memo1[p];
  }

  double estimateAP50(int n_positive, int n_all, double ratio) {
    // score2(n, m, p) := estimateAP50(n_positive, n_all-n_positive, ratio)
    pair<int, int> p = {n_positive, n_all};
    if (memo2.count(p)) return memo2[p];

    if (n_positive == n_all) return estimateAP50(n_positive, n_all);
    double prob_sum = 0;
    double score = 0;
    int n = n_all-n_positive;
    REP(k,n+1) {
      double prob = binomial_pmf(n, ratio, k);
      prob_sum += prob;
      double ap50 = estimateAP50(n_positive, n_positive+k);
      score += prob * ap50;
      // D1(k, prob, prob_sum, ap50, score);
    }
    memo2[p] = score;
    return memo2[p];
  }
};
Scorer scorer_;

double calculate_mean(const std::vector<double>& v) {
  double sum = std::accumulate(v.begin(), v.end(), 0.0);
  return sum / v.size();
}

double calculate_median(std::vector<double> v) {
  size_t size = v.size();
  if (size == 0) {
    return 0;
  }
  std::sort(v.begin(), v.end());
  if (size % 2 == 0) {
    return (v[size / 2 - 1] + v[size / 2]) / 2;
  } else {
    return v[size / 2];
  }
}

double calculate_max(const std::vector<double>& v) {
  return *std::max_element(v.begin(), v.end());
}

double calculate_min(const std::vector<double>& v) {
  return *std::min_element(v.begin(), v.end());
}

void show_stats(const std::vector<double>& v) {
  double mean = calculate_mean(v);
  double median = calculate_median(v);
  double max = calculate_max(v);
  double min = calculate_min(v);
  D1(mean, median, max, min);
}

using vocab_t = unordered_map<string, int>;
using id_list_t = vector<vector<int>>;

// title/abstract/claims/description/cpc_codes
array<vector<string>,5> id2x_;
array<vocab_t,5> vocabs_;
array<vocab_t,5> ids_;
array<id_list_t,5> x2pubnums_, pubnum2xs_;

vector<string> id2pubnum_;
vocab_t vocab_pubnum_;
vocab_t id_pubnum_;

int f_ = 4;

int read_x2y(const string &filepath, id_list_t &ids) {
  // cpc2pubnum, pubnum2cpc
  ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "Failed to open file:" << filepath << std::endl;
    return 1;
  }
  string contents((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
  std::istringstream iss(contents);
  int n; iss >> n;
  ids.resize(n);
  REP(i,n) {
    int m; iss >> m;
    ids[i].resize(m);
    REP(j,m) {
      iss >> ids[i][j];
    }
    sort(ALL(ids[i]));
    if (ids[i].size() > 0) {
      N_PUBNUM = max(N_PUBNUM, ids[i].back()+1);
    }
  }
  // D1(ids.size(), ids.back());
  return 0;
}

int read_nshot(const string &filepath, unordered_map<int, string>& vocab) {
  ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "Failed to open file:" << filepath << std::endl;
    return 1;
  }
  std::string line;

  while (std::getline(file, line)) {
    std::istringstream line_stream(line);
    std::string token;

    int key;
    std::string value;

    if (std::getline(line_stream, token, '\t')) {
      key = std::stoi(token);
    } else {
      continue;
    }

    if (std::getline(line_stream, value)) {
      vocab[key] = value;
    }
  }

  file.close();
  return 0;
}

int read_vocab(const string &filepath, vocab_t& vocab) {
  ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "Failed to open file:" << filepath << std::endl;
    return 1;
  }
  string contents((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
  std::istringstream iss(contents);
  while (true) {
    string key; 
    int value;
    iss >> key >> value;
    if (key == "") break;
    vocab[key] = value;
  }
  // D1(vocab.size());
  file.close();
  return 0;
}

int read_vocab(const string &filepath, vocab_t& vocab, vocab_t& ids, vector<string> &id2key) {
  ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "Failed to open file:" << filepath << std::endl;
    return 1;
  }
  string contents((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
  std::istringstream iss(contents);
  while (true) {
    string key; 
    int value;
    iss >> key >> value;
    if (key == "") break;
    vocab[key] = value;
    ids[key] = ids.size();
    id2key.push_back(key);
  }
  // D1(id2key.front(), id2key.back());
  file.close();
  return 0;
}

vector<vector<int>> testcases_;

int read_testcases(const string &filepath, vector<vector<int>> &testcases) {
  ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "Failed to open file:" << filepath << std::endl;
    return 1;
  }
  string line;
  while (getline(file, line)) {
    std::istringstream iss(line);
    vector<int> testcase(50, -1);
    REP(i,51) {
      if (i == 0) {
        int query_pubnum;
        iss >> query_pubnum;
        LAST_TEST_ID = max(LAST_TEST_ID, query_pubnum);
        continue;
      }
      iss >> testcase[i-1];
      LAST_TEST_ID = max(LAST_TEST_ID, testcase[i-1]);
    }
    assert (testcase.back() != -1);
    assert (testcase.back() <= LAST_TEST_ID);
    sort(ALL(testcase));
    testcases.emplace_back(testcase);
    // REP(i,50) {
    //   D1(pubnum2xs_[f_][testcase[i]]);
    // }
    // cerr << '\n';
    // break;
  }
  file.close();
  D1(testcases.size());
  D1(LAST_TEST_ID);
  return 0;
}

int read_active_testids(const string &filepath, set<int> &active_testids) {
  ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "Failed to open file:" << filepath << std::endl;
    return 1;
  }
  string line;
  while (getline(file, line)) {
    std::istringstream iss(line);
    int pubnum;
    iss >> pubnum;
    active_testids.insert(pubnum);
  }
  file.close();
  return 0;
}

struct MyVec {
  int n = 0; // size
  vector<int> v;
  MyVec() : n(0), v() {}
  MyVec(const vector<int> &v_): n(v_.size()), v(v_) {}
  size_t size() const { return n; }
  void resize(int n_) {
    n = n_;
    v.resize(n);
    REP(i,n) { v[i] = i; }
  }
  int& operator[](size_t i) { return v[i]; }
  const int& operator[](size_t i) const { return v[i]; }
  friend std::ostream& operator<<(std::ostream& os, const MyVec &x) {
    os << '[';
    for (int i = 0; i < x.n; ++i) {
      os << x[i] << ',';
    }
    os << ']';
    return os;
  }
  vector<int> to_vec() const  {
    vector<int> ret(n);
    REP(i,n) ret[i] = v[i];
    return std::move(ret);
  }
  MyVec to_myvec() const {
    MyVec ret;
    ret.n = n;
    ret.v.resize(n);
    REP(i,n) ret[i] = v[i];
    return std::move(ret);
  }
};

int difference(const MyVec &m1, const MyVec &m2, vector<int> &diff) {
  // calculate the difference of two lists
  std::set_difference(ALL(m1.v), ALL(m2.v), std::back_inserter(diff));
  return diff.size();
}

MyVec intersect_, intersect0_;

template <typename T1, typename T2>
int intersect(const T1 &v1, const T2 &v2, int limit=INF) {
  /*
    calculate the intersection of two lists
    To avoid having to allocate the output vector each time, the output is stored in intersect_.

    Note:
      v1 and v2 must be sorted in ascending order.

    Examples:
      vector<int> v1 = {1,4,9};
      vector<int> v2 = {0,1,2,3,4};
      -> intersect_ = {1,4};
  */

  size_t i = 0, j = 0;
  int offset = 0;
  
  while (i < v1.size() && j < v2.size()) {
    if (v1[i] > limit || v2[j] > limit) break;
    if (v1[i] < v2[j]) {
      ++i;
    } else if (v1[i] > v2[j]) {
      ++j;
    } else {
      intersect_[offset++] = v1[i];
      ++i;
      ++j;
    }
  }
  intersect_.n = offset;
  assert (intersect_.n <= v1.size() && intersect_.n <= v2.size());
  return intersect_.n;
}

int ENTITY_TYPE_NONE = 0;
int ENTITY_TYPE_NSHOT = 1;
int ENTITY_TYPE_CONJUNCTIVE = 2;
int ENTITY_TYPE_COMPOSITE = 3;

struct IEntity {
  MyVec positive;
  vector<int> potential_negative;
  size_t n_all;
  int type = ENTITY_TYPE_NONE;
  IEntity() {}
  IEntity(const MyVec &positive_, size_t n_all_, int type_): 
    positive(positive_), potential_negative(), n_all(n_all_), type(type_) {}
  IEntity(const MyVec &positive_, const vector<int> &potential_negative_, int type_): 
    positive(positive_), potential_negative(potential_negative_), type(type_) {
      n_all = positive.size() + potential_negative.size();
    }
  virtual int countTokens() const = 0;
  virtual vector<int> listupPositive() const = 0;
  virtual vector<int> listupPotentialNegative() const = 0;
  double score(int n_positive) const {
    if (n_positive == 0) return 0;
    // return pow(n_positive, 2.0) / (double)n_all;
    // return n_positive;
    return scorer_.evaluate(n_positive, n_all) + n_positive/(double)(countTokens()) * 0.01;
    // return special_token.size() * 0.00001 + n_positive/(double)(n_token+1) * 0.01 +  n_positive / (double)n_all;
  }
  double score() const {
    int n_positive = positive.size();
    return score(n_positive);
  }
  double score(MyVec &remain) const {
    int n_positive = intersect(remain, positive);
    return score(n_positive);
  }
  virtual shared_ptr<IEntity> clone() const = 0;
  virtual string toQuery() const = 0;
  void finalize(const unordered_map<int, int> &positive2id) {
    // Convert positives into a sequence starting from 0.
    REP(i, positive.size()) {
      assert (positive2id.count(positive[i]) != 0);
      positive[i] = positive2id.at(positive[i]);
    }
  }
  friend std::ostream& operator<<(std::ostream& os, const IEntity &x) {
    os << '[';
    os << x.positive;
    os << ",n=" << x.n_all;
    os << ",t=" << x.countTokens();
    os << ']';
    return os;
  }
};

using entity_ptr = shared_ptr<IEntity>;

// conjunctive subquery
struct Entity: IEntity {
  vector<pair<int, int>> tokens;
  int n_token = 0;
  Entity() {}
  Entity(const vector<pair<int,int>> &tokens_, const MyVec &positive_, size_t n_all_): 
    tokens(tokens_), n_token(tokens_.size()+1), IEntity(positive_, n_all_, ENTITY_TYPE_CONJUNCTIVE) {}
  Entity(const vector<pair<int,int>> &tokens_, const MyVec &positive_, const vector<int> &potential_negative_):
    tokens(tokens_), n_token(tokens_.size()+1), IEntity(positive_, potential_negative_, ENTITY_TYPE_CONJUNCTIVE) {}
  int countTokens() const override { return n_token; }
  vector<int> listupPositive() const override {
    return positive.v;
  }
  vector<int> listupPotentialNegative() const override {
    return potential_negative;
  }
  string toQuery() const override {
    string query;
    query += "(";
    REP(i, tokens.size()) {
      int f = tokens[i].first;
      int xid = tokens[i].second;
      query += x2field_[f] +  ":" + id2x_[f][xid];
      if (i < tokens.size()-1) query += " ";
    }
    query += ")"; 
    return query;
  }
  shared_ptr<IEntity> clone() const override { return make_shared<Entity>(*this); }
};

// n-shot subquery
struct NshotEntity: IEntity {
  string special_token;
  int n_token = 0;
  NshotEntity() {}
  NshotEntity(const string &special_token_, int pubnum): 
    special_token(special_token_), IEntity(MyVec({pubnum}), 1, ENTITY_TYPE_NSHOT) {
      n_token = 2;
      if (special_token[0] == '(') n_token = 3;
    }
  int countTokens() const override { return n_token; }
  vector<int> listupPositive() const override {
    return positive.v;
  }
  vector<int> listupPotentialNegative() const override {
    return potential_negative;
  }
  shared_ptr<IEntity> clone() const override { return make_shared<NshotEntity>(*this); }
  string toQuery() const override {
    return special_token;
  }
};

struct CompositeEntity: IEntity {
  vector<entity_ptr> entities;
  int field_id = -1; // the field ID of the token to be shared.
  int word_id = -1; // the word ID of the token to be shared.
  CompositeEntity(const vector<entity_ptr> &entities_, int field_id_, int word_id_): entities(entities_), field_id(field_id_), word_id(word_id_) {
    type = ENTITY_TYPE_COMPOSITE;
  }
  int countTokens() const override { 
    int n_token = 1;
    for (const auto &e: entities) {
      assert (e->type == ENTITY_TYPE_CONJUNCTIVE);
      n_token += e->countTokens() - 1;
    }
    return n_token;
  }
  vector<int> listupPositive() const override {
    // FIXME: deduplicate
    vector<int> ret;
    for (const auto &e: entities) {
      vector<int> tmp = e->listupPositive();
      ret.insert(ret.end(), ALL(tmp));
    }
    return ret;
  }
  vector<int> listupPotentialNegative() const override {
    // FIXME: deduplicate
    vector<int> ret;
    for (const auto &e: entities) {
      vector<int> tmp = e->listupPotentialNegative();
      ret.insert(ret.end(), ALL(tmp));
    }
    return ret;
  }
  string toQuery() const override {
    string query;
    query += "(";
    query += x2field_[field_id] +  ":" + id2x_[field_id][word_id];
    query += " (";
    REP(j,entities.size()) {
      shared_ptr<Entity> e = dynamic_pointer_cast<Entity>(entities[j]);
      if (j > 0) query += " OR ";
      vector<pair<int,int>> tokens;
      REP(i, e->tokens.size()) {
        int f = e->tokens[i].first;
        int xid = e->tokens[i].second;
        if (f == field_id && xid == word_id) continue;
        tokens.emplace_back(e->tokens[i]);
      }
      if (tokens.size() > 1) query += "(";
      REP(i, tokens.size()) {
        int f = tokens[i].first;
        int xid = tokens[i].second;
        query += x2field_[f] +  ":" + id2x_[f][xid];
        if (i < tokens.size()-1) query += " ";
      }
      if (tokens.size() > 1) query += ")";
    }
    query += ")";
    query += ")"; 
    return query;
  }
  shared_ptr<IEntity> clone() const override { return make_shared<CompositeEntity>(*this); }
};

string generate_query(const vector<entity_ptr> &entities) {
  if (entities.size() == 0) return "ti:dummydummydummy";
  string query;
  REP(j, entities.size()) {
    const entity_ptr &e = entities[j];
    if (e->countTokens() == 0) continue;
    if (j > 0) query += " OR ";
    query += e->toQuery();
  }
  return query;
}

struct BeamState {
  double score = 0;
  uint64_t bit_positive = 0; // Manage flags indicating the presence of Positives in the lower 50 digits.
  uint64_t n_potential_negative = 0;
  int n_token = -1; // Each token assumes that one token will be added for the OR, so subtract 1.
  vector<entity_ptr> entities;

  BeamState() {}
  double getScore() const { return score; }

  uint64_t calcHash() const {
    uint64_t hash = bit_positive;
    hash |= ((uint64_t)n_token << 50);
    hash |= (n_potential_negative << 56);
    return hash;
  }

  void add(const entity_ptr &e) {
    for (int pid: e->positive.v) {
      bit_positive |= (1ULL << pid);
    }
    assert (e->type != ENTITY_TYPE_COMPOSITE);
    // assert (e->n_all>=e->positive.size());
    n_potential_negative += (e->n_all - e->positive.size());
    n_token += e->countTokens();
    entities.push_back(e);
  }

  void addComposite(int add_offset_id, int field_id, int word_id, const entity_ptr &e) {
    assert (add_offset_id != -1);
    assert (field_id != -1);
    assert (word_id != -1);
    assert (e->type == ENTITY_TYPE_CONJUNCTIVE);
    for (int pid: e->positive.v) {
      bit_positive |= (1ULL << pid);
    }
    n_token += e->countTokens() - 1;
    n_potential_negative += (e->n_all - e->positive.size());
    assert (add_offset_id < entities.size());
    entity_ptr &e0 = entities[add_offset_id];
    assert (e0->type != ENTITY_TYPE_NSHOT);
    if (e0->type == ENTITY_TYPE_COMPOSITE) {
      shared_ptr<CompositeEntity> e1 = dynamic_pointer_cast<CompositeEntity>(e0);
      assert (e1->field_id == field_id && e1->word_id == word_id);
      vector<entity_ptr> es = e1->entities;
      es.push_back(e);
      CompositeEntity e2(es, field_id, word_id);
      entities[add_offset_id] = make_shared<CompositeEntity>(e2);
    }
    else if (e0->type == ENTITY_TYPE_CONJUNCTIVE) {
      vector<entity_ptr> es = {e0, e};
      CompositeEntity e1(es, field_id, word_id);
      entities[add_offset_id] = make_shared<CompositeEntity>(e1);
    }
    else {
      assert (false);
    }
  }

  int countPositive() const {
    return __builtin_popcountll(bit_positive);
  }

  double calcScore() const { 
    int n_positive = countPositive();
    return scorer_.evaluate(n_positive, n_positive+n_potential_negative, n_token);
    // return scorer_.evaluate(n_positive, n_positive+n_potential_negative);
  }

  vector<int> listupPositive(const vector<int> &testcase) {
    set<int> positive;
    for (const auto &e: entities) {
      vector<int> tmp = e->listupPositive();
      for (int x: tmp) { positive.insert(testcase[x]); }
    }
    vector<int> ret(ALL(positive));
    return ret;
  }

  vector<int> listupPotentialNegative() {
    set<int> potential_negative;
    for (const auto &e: entities) {
      vector<int> tmp = e->listupPotentialNegative();
      for (int x: tmp) { potential_negative.insert(x); }
    }
    vector<int> ret(ALL(potential_negative));
    return ret;
  }
};

void generate_nshot_entities(const vector<int> &testcase, vector<entity_ptr> &entities) {
  array<unordered_map<string, int>,5> word2id;
  for (int pubnum: testcase) {
    for (int f: param_.active_fields) {
      for (int x: pubnum2xs_[f][pubnum]) {
        word2id[f][id2x_[f][x]] = pubnum;
      }
    }
  }

  for (int pubnum: testcase) {
    if (!nshot_.count(pubnum)) continue;
    NshotEntity entity(nshot_[pubnum], pubnum);
    entities.emplace_back(make_shared<NshotEntity>(entity));
  }
}

void deduplicate_entities(vector<entity_ptr> &entities) {
  vec_hash_pii_t hashmap;
  REP(i,entities.size()) {
    entity_ptr &e = entities[i];
    vector<int> hvec = e->positive.to_vec();
    hvec.push_back(e->countTokens());
    if (hashmap.find(hvec) != hashmap.end()) {
      pair<int, int> p = hashmap[hvec];
      if (p.first <= e->n_all) continue;
      else {
        hashmap[hvec] = {e->n_all, i};
      }
    }
    else {
      hashmap[hvec] = {e->n_all, i};
    }
  }
  vector<entity_ptr> new_entities;
  for (auto &it: hashmap) {
    new_entities.emplace_back(entities[it.second.second]);
  }
  // D1(entities.size(), hashmap.size());
  swap(entities, new_entities);
}

void copy_vector(const vector<int> &from, MyVec &to, int limit) {
  // Assume `from` is already sorted. Copy elements from `from` to `to` up to the value `limit`.
  int size = 0;
  for (int x : from) {
    if (x > limit) break;
    ++size;
  }

  to.resize(size);

  for (int i = 0; i < size; ++i) {
    to[i] = from[i];
  }
}

Timer dfs_timer_;
void dfs_conjunctive(int pubnum, const MyVec &positive, const MyVec &test_all, const MyVec &all, int max_depth, vector<pair<int, int>> &used, vec_hash_t &hashmap, vector<entity_ptr> &entities) {
  if (dfs_timer_.get() > param_.dfs_timeout_sec) return;
  int best_score = INF;
  assert (positive.size() > 1);
  if (positive.size() == test_all.size()) {
    vector<int> hvec = positive.v;
    hvec.push_back(used.size()); // Consider the number of tokens as well.
    if (hashmap.find(hvec) != hashmap.end()) {
      best_score = hashmap[hvec];
      if (best_score == positive.size()) return;
      if (used.size() == max_depth && all.size() >= best_score) return;
    }
    if (positive.size() == all.size() || used.size() == max_depth) {
      if (hashmap.find(hvec) == hashmap.end() || hashmap[hvec] > all.size()) {
        hashmap[hvec] = all.size();
      }
    }
    // if ((positive.size() >= all.size())) {
    if ((positive.size()+1 >= all.size())) {
    // if ((positive.size()+2 >= all.size())) {
    // if ((positive.size()+10 >= all.size())) {
    // if ((positive.size()+50 >= all.size())) {
    // if ((positive.size()+100 >= all.size())) {
      // D1(pubnum, used, positive, all.size());
      // assert (positive.size() == test_all.size());
      vector<int> potential_negative;
      difference(all, positive, potential_negative);
      assert (positive.size() + potential_negative.size() == all.size());
      entities.emplace_back(std::make_shared<Entity>(used, positive, potential_negative));
    }
    // D1(pubnum, used, positive, all.size());
    if (positive.size() == all.size()) return;
  }
  if (used.size() == max_depth) return;
  for (int f: param_.active_fields) {
    if (used.size() > 0 && field_order_[f] < field_order_[used.back().first]) continue;
    if (pubnum2xs_[f][pubnum].size() <= 1) continue;
    for (int x: pubnum2xs_[f][pubnum]) {
      if (used.size() > 0) {
        auto &it = used.back();
        if (f == it.first && x <= it.second) continue;
      }
      const auto &pubnums = x2pubnums_[f][x];
      int n = intersect(positive, pubnums, LAST_TEST_ID);
      assert (n > 0);
      if (n == 1) continue;
      MyVec new_positive = intersect_.to_myvec();

      // Skip since a good candidate has already been found. 
      if (hashmap.find(new_positive.v) != hashmap.end() && hashmap[new_positive.v] == new_positive.size()) continue;

      MyVec new_test_all;
      if (test_all.size() == 0) {
        copy_vector(pubnums, new_test_all, LAST_TEST_ID);
      }
      else {
        int m = intersect(test_all, pubnums, LAST_TEST_ID);
        new_test_all = intersect_.to_myvec();
      }
      if (used.size() == max_depth-1 && new_positive.size() != new_test_all.size()) continue;


      MyVec new_all;
      if (all.size() == 0) new_all = pubnums;
      else {
        int m = intersect(all, pubnums);
        assert (m >= n);
        new_all = intersect_.to_myvec();
      }
      used.emplace_back(f, x);
      dfs_conjunctive(pubnum, new_positive, new_test_all, new_all, max_depth, used, hashmap, entities);
      used.pop_back();
      if (dfs_timer_.get() > param_.dfs_timeout_sec) return;
    }
  }
  return;
}

class BeamStateComparator {
public:
  bool operator()(const BeamState& a, const BeamState& b)
  {
    return (a.score < b.score);
  }
};

using pq_t = priority_queue<BeamState, vector<BeamState>, BeamStateComparator>;

void listup_beam_states(const BeamState &s, const vector<entity_ptr> &candidates, vector<pq_t> &pqs, unordered_map<uint64_t, int> &dedup) {
  assert(pqs.size() == MAX_TOKEN + 1);
  int n_positive = s.countPositive();
  for (const auto &e: candidates) {

    int field_id = -1;
    int word_id = -1;
    vector<pair<int, int>> shared_tokens;
    int add_offset_id = -1;
    if (e->type == ENTITY_TYPE_CONJUNCTIVE) {
      shared_ptr<Entity> e1 = dynamic_pointer_cast<Entity>(e);
      REP(i,s.entities.size()) {
        const auto &e0 = s.entities[i];
        if (e == e0) continue;
        if (e0->type == ENTITY_TYPE_COMPOSITE) {
          shared_ptr<CompositeEntity> e2 = dynamic_pointer_cast<CompositeEntity>(e0);
          for (const auto &[field_id1, word_id1]: e1->tokens) {
            if (field_id1 == e2->field_id && word_id1 == e2->word_id) {
              add_offset_id = i;
              field_id = field_id1;
              word_id = word_id1;
              break;
            }
          }
          if (add_offset_id != -1) break;
        }
        else if (e0->type == ENTITY_TYPE_CONJUNCTIVE) {
          shared_ptr<Entity> e2 = dynamic_pointer_cast<Entity>(e0);
          for (const auto &[field_id1, word_id1]: e1->tokens) {
            for (const auto &[field_id2, word_id2]: e2->tokens) {
              if (field_id1 == field_id2 && word_id1 == word_id2) {
                field_id = field_id1;
                word_id = word_id1;
                add_offset_id = i;
                break;
              }
            }
            if (add_offset_id != -1) break;
          }
          if (add_offset_id != -1) break;
        }
      }
    }

    int next_n_token = s.n_token + e->countTokens();
    if (add_offset_id != -1) next_n_token -= 1;
    assert (next_n_token > 0);
    if (next_n_token > MAX_TOKEN) continue;
    int n_positive_diff = 0;
    int n_potential_negative_diff = e->n_all - e->positive.size();
    assert (n_potential_negative_diff >= 0);
    REP(i,e->positive.size()) {
      if (~s.bit_positive & (1ULL<<(e->positive[i]))) n_positive_diff++;
    }
    if (n_positive_diff == 0) continue;
    int next_n_positive = n_positive + n_positive_diff;
    int next_n_all = next_n_positive + s.n_potential_negative + n_potential_negative_diff;
    // In the early stages, avoid increasing potential negatives too easily.
    // double score = scorer_.evaluate(next_n_positive, next_n_all) - (next_n_all-next_n_positive)*(50-next_n_token)*0.0001;
    double score = scorer_.evaluate(next_n_positive, next_n_all, next_n_token);
    pq_t &pq = pqs[next_n_token];
    if (pq.size() == param_.beam_width) {
      const BeamState &border = pq.top();
      if (border.score < score) {
        BeamState next = s;
        if (add_offset_id == -1) next.add(e);
        else {
          next.addComposite(add_offset_id, field_id, word_id, e);
        }
        // assert (score == next.calcScore());
        next.score = score;
        uint64_t hash = next.calcHash();
        if (dedup.count(hash) && dedup[hash] <= next_n_all) {
          // deduplicates since there is a state as good as or better than the current one.
          continue;
        }
        dedup[hash] = next_n_all;
        pq.pop();
        pq.push(next);
        // D1(*e, next_n_token, n_positive_diff, n_potential_negative_diff, score);
      }
    }
    else {
      BeamState next = s;
      if (add_offset_id == -1) next.add(e);
      else {
        next.addComposite(add_offset_id, field_id, word_id, e);
      }
      // assert (score == next.calcScore());
      next.score = score;
      uint64_t hash = next.calcHash();
      if (dedup.count(hash) && dedup[hash] <= next_n_all) {
          // deduplicates since there is a state as good as or better than the current one.
        continue;
      }
      dedup[hash] = next_n_all;
      pq.push(next);
    }
  }
}

struct ISolver {
  ISolver() {}
  virtual void solve() = 0;
  void generateEntities(const vector<int> &testcase, vector<entity_ptr> &entities) {
    generate_nshot_entities(testcase, entities);
    vec_hash_t hashmap;
    REP(i,50) {
      int pubnum = testcase[i];
      vector<pair<int, int>> used;
      MyVec test_all;
      MyVec all;
      dfs_timer_.reset();
      dfs_conjunctive(pubnum, testcase, test_all, all, param_.max_depth, used, hashmap, entities);
    }
    deduplicate_entities(entities);
    unordered_map<int, int> positive2id;
    assert (testcase.size() == 50);
    REP(i,testcase.size()) {
      positive2id[testcase[i]] = i;
    }
    for (auto &e: entities) {
      e->finalize(positive2id);
    }
  }
};

using solver_ptr = shared_ptr<ISolver>;

struct BeamSolver: ISolver {
  bool positive_only = false;  // If true, potential negatives are not allowed.
  BeamSolver() {}
  BeamSolver(bool positive_only_): positive_only(positive_only_) {}
  void solve_one(const vector<entity_ptr> &entities, BeamState &best) {
    vector<entity_ptr> new_entities;
    if (positive_only) {
      for (const auto &e: entities) {
        if (e->n_all > e->positive.size()) continue;
        new_entities.push_back(e);
      }
    }
    else {
      new_entities = entities;
    }
    assert (best.score == 0 && best.n_token == -1);

    vector<pq_t> states(MAX_TOKEN+1);
    unordered_map<uint64_t, int> dedup; // deduplication
    states[0].push(best);
    REP(turn,MAX_TOKEN) {
      while (!states[turn].empty()) {
        BeamState cur = states[turn].top(); states[turn].pop();
        assert (turn == 0 || cur.n_token == turn);
        if (cur.score > best.score) best = cur;
        listup_beam_states(cur, new_entities, states, dedup);
      }
    }
    while (!states[MAX_TOKEN].empty()) {
      BeamState cur = states[MAX_TOKEN].top(); states[MAX_TOKEN].pop();
      if (cur.score > best.score) best = cur;
    }
  }

  void solve() override {
    vector<double> scores;
    REP(j,testcases_.size()) {
      if (active_testids_.count(j) == 0) continue;
      Timer timer;
      BeamState best;
      vector<entity_ptr> entities;
      generateEntities(testcases_[j], entities);
      solve_one(entities, best);

      scores.push_back(best.score);
      double mean = calculate_mean(scores);
      int n_entity = entities.size();
      int positive =  best.countPositive();
      int potential_negative = best.n_potential_negative;
      double score = best.score;
      int n_token = best.n_token;
      D1(j, n_entity, n_token, positive, potential_negative, score, mean, timer.get());
      string query = generate_query(best.entities);
      cout << query << '\n';
    }
    show_stats(scores);
  }
};

struct MultiSolver: ISolver {
  void solve_one(const vector<entity_ptr> &entities, BeamState &best) {
    BeamSolver solver1(true), solver2(false);
    BeamState best1, best2;
    solver1.solve_one(entities, best1);
    if (best1.score == 1) {
      // A complete solution has been found, so the process is terminated.
      best = best1;
      return;
    }
    solver2.solve_one(entities, best2);
    if (best1.score >= best2.score) best = best1;
    else best = best2;
  }
  void solve() override {
    vector<double> scores;
    REP(j,testcases_.size()) {
      if (active_testids_.count(j) == 0) continue;
      Timer timer;
      BeamState best;
      vector<entity_ptr> entities;
      generateEntities(testcases_[j], entities);
      solve_one(entities, best);

      scores.push_back(best.score);
      double mean = calculate_mean(scores);
      int n_entity = entities.size();
      int positive =  best.countPositive();
      int potential_negative = best.n_potential_negative;
      double score = best.score;
      int n_token = best.n_token;
      D1(j, n_entity, n_token, positive, potential_negative, score, mean, timer.get());
      string query = generate_query(best.entities);
      cout << query << '\n';
    }
    show_stats(scores);
  }
};

int main(int argc, char* argv[]) 
{
  argparse::ArgumentParser program("run");

  program.add_argument("-f", "--field")
  .default_value<std::vector<std::string>>({})
  .append()
  .help("Used fields (cpc|title|abstract|claims)");

  program.add_argument("-t", "--datadir")
  .default_value<std::string>("")
  .help("Path to datadir");

  program.add_argument("--beam-width")
  .store_into(param_.beam_width)
  .help("BEAM width");

  program.add_argument("--max-depth")
  .store_into(param_.max_depth)
  .help("DFS max depth");

  program.add_argument("--sampling-ratio")
  .store_into(param_.sampling_ratio)
  .help("Sampling ratio for score calculation");

  program.add_argument("--timeout")
  .store_into(param_.dfs_timeout_sec)
  .help("Timeout second of DFS");

  string active_testids_file;
  program.add_argument("--active-testids")
  .store_into(active_testids_file)
  .help("List of active testids");

  program.add_argument("--solver")
  .store_into(param_.solver)
  .help("Used solver (beam|multi)");

  try {
    program.parse_args(argc, argv);    // Example: ./main --color orange
  }
  catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    std::exit(1);
  }
  auto fields = program.get<vector<string>>("--field");
  auto datadir = program.get<string>("--datadir");
  if (datadir.size() > 0) DATADIR = datadir;
  D1(DATADIR);

  ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  {
    read_testcases(DATADIR + "test.tsv", testcases_);

    read_nshot(DATADIR + "nshot.tsv", nshot_);

    read_vocab(DATADIR + "vocab_pubnum.tsv", vocab_pubnum_, id_pubnum_, id2pubnum_);
    int field_order = 0;
    for (const string &field: fields) {
      int f = field2x_[field];
      string vocab_file = DATADIR + "vocab_" + field + ".tsv";
      string x2pubnum_file = DATADIR + field + "2pubnum.txt";
      string pubnum2x_file = DATADIR + "pubnum2" + field + ".txt";
      read_vocab(vocab_file, vocabs_[f], ids_[f], id2x_[f]);
      read_x2y(x2pubnum_file, x2pubnums_[f]);
      read_x2y(pubnum2x_file, pubnum2xs_[f]);
      param_.active_fields.push_back(f);
      field_order_[f] = field_order++;
    }
    D1(param_);

    D1(N_PUBNUM);
    intersect_.resize(N_PUBNUM);
    intersect0_.resize(N_PUBNUM);
    scorer_ = Scorer(param_.sampling_ratio);
  }

  if (active_testids_file.size() > 0) {
    read_active_testids(active_testids_file, active_testids_);
  }
  else {
    REP(pubnum,testcases_.size()) {
      active_testids_.insert(pubnum);
    }
  }

  solver_ptr solver;
  if (param_.solver == "beam") {
    solver = make_shared<BeamSolver>();
  }
  else if (param_.solver == "multi") {
    solver = make_shared<MultiSolver>();
  }
  else {
    std::cerr << "Undefined solver: " << param_.solver << std::endl;
    exit(1);
  }
  solver->solve();

  return 0;
}