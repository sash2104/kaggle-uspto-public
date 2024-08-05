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
  double dfs_timeout_sec = 0.01;
  bool log_level_info = false;
  int max_depth = 2;
  vector<int> active_fields;
  Param() {
  }
  friend std::ostream& operator<<(std::ostream& os, const Param &p) {
    os << "max_depth=" << p.max_depth;
    os << ",timeout=" << p.dfs_timeout_sec;
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

struct pair_hash {
  std::size_t operator() (const std::pair<int, int>& pair) const {
    return pair.second * 51 + pair.first;
  }
};

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
      if (offset == 2) { // Because it does not become a twoshot subquery, pruning
        intersect_.n = 2;
        return intersect_.n;
      }
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
    return 0;
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
bool dfs_conjunctive(int pubnum, const MyVec &positive, const MyVec &all, int max_depth, vector<pair<int, int>> &used, vector<entity_ptr> &entities) {
  if (dfs_timer_.get() > param_.dfs_timeout_sec) return false;
  int best_score = INF;
  if (positive.size() == all.size()) {
    vector<int> potential_negative;
    entities.emplace_back(std::make_shared<Entity>(used, positive, potential_negative));
    // found twoshot subquery
    return true;
  }
  if (used.size() == max_depth) return false;
  for (int f: param_.active_fields) {
    if (used.size() > 0 && field_order_[f] < field_order_[used.back().first]) continue;
    if (pubnum2xs_[f][pubnum].size() <= 1) continue;
    for (int x: pubnum2xs_[f][pubnum]) {
      if (used.size() > 0) {
        auto &it = used.back();
        if (f == it.first && x <= it.second) continue;
      }
      const auto &pubnums = x2pubnums_[f][x];
      int n = intersect(positive, pubnums);
      if (n < 1) continue;
      MyVec new_positive = intersect_.to_myvec();

      MyVec new_all;
      if (all.size() == 0) new_all = pubnums;
      else {
        int m = intersect(all, pubnums);
        if (m > 1) continue;
        assert (m >= n);
        new_all = intersect_.to_myvec();
      }
      used.emplace_back(f, x);
      bool found = dfs_conjunctive(pubnum, new_positive, new_all, max_depth, used, entities);
      if (found) return true;
      used.pop_back();
      if (dfs_timer_.get() > param_.dfs_timeout_sec) return false;
    }
  }
  return false;
}

void find_twoshot() {
  // REP(j,100000) {
  REP(j,N_PUBNUM) {
    Timer timer;
    int pubnum = j;
    vector<pair<int, int>> used;
    MyVec test_all;
    MyVec all;
    vector<int> testcase = {pubnum};
    vector<entity_ptr> entities;
    dfs_timer_.reset();
    dfs_conjunctive(pubnum, testcase, all, param_.max_depth, used, entities);
    if (entities.size() > 0) {
      string query = generate_query(entities);
      cout << id2pubnum_[j] << '\t' << query << '\n';
    }
  }
}

int main(int argc, char* argv[]) 
{
  argparse::ArgumentParser program("run");

  program.add_argument("-f", "--field")
  .default_value<std::vector<std::string>>({})
  .append()
  .help("Used fields (cpc|title|abstract|claims|description)");

  program.add_argument("-t", "--datadir")
  .default_value<std::string>("")
  .help("Path to datadir");

  program.add_argument("--timeout")
  .store_into(param_.dfs_timeout_sec)
  .help("Timeout second of DFS");

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
  }

  find_twoshot();

  return 0;
}