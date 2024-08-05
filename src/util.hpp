#ifndef __UTIL__
#define __UTIL__

// #include <bits/stdc++.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#define REP(i, n) for (int i = 0; (i) < (int)(n); ++ (i))
#define REP3(i, m, n) for (int i = (m); (i) < (int)(n); ++ (i))
#define REPR(i, n) for (int i = (int)(n) - 1; (i) >= 0; -- (i))
#define REP3R(i, m, n) for (int i = (int)(n) - 1; (i) >= (int)(m); -- (i))
#define ALL(x) std::begin(x), std::end(x)

// Debug

template<class T>
void print_collection(std::ostream& out, T const& x);

template<class A>
std::ostream& operator<<(std::ostream& out, std::vector<A> const& x) { print_collection(out, x); return out; }
template<class A, size_t N>
std::ostream& operator<<(std::ostream& out, std::array<A, N> const& x) { print_collection(out, x); return out; }
template<class A>
std::ostream& operator<<(std::ostream& out, std::set<A> const& x) { print_collection(out, x); return out; }
template<class A>
std::ostream& operator<<(std::ostream& out, std::unordered_set<A> const& x) { print_collection(out, x); return out; }
template<class A, class B>
std::ostream& operator<<(std::ostream& out, std::map<A, B> const& x) { print_collection(out, x); return out; }


template<class A, class B>
std::ostream& operator<<(std::ostream& out, std::pair<A, B> const& x) {
  out << '(';
  out << x.first;
  out << ',';
  out << x.second;
  out << ')';
  return out;
}

template<class T, size_t... I>
void print_tuple(std::ostream& out, T const& a, std::index_sequence<I...>);
template<class... A>
std::ostream& operator<<(std::ostream& out, std::tuple<A...> const& x) {
  print_tuple(out, x, std::index_sequence_for<A...>{});
  return out;
}

template<class T, size_t... I>
void print_tuple(std::ostream& out, T const& a, std::index_sequence<I...>){
  using swallow = int[];
  out << '(';
  (void)swallow{0, (void(out << (I == 0? "" : ", ") << std::get<I>(a)), 0)...};
  out << ')';
}

template<class T>
void print_collection(std::ostream& out, T const& x) {
  int f = 0;
  out << '[';
  for(auto const& i: x) {
    out << (f++ ? "," : "");
    out << i;
  }
  out << "]";
}

static inline void d1_impl_seq() { std::cerr << "}"; }
template <class T, class... V>
void d1_impl_seq(T const& t, V const&... v) {
  std::cerr << t;
  if(sizeof...(v)) { std::cerr << ", "; }
  d1_impl_seq(v...);
}

static inline void d2_impl_seq() { }
template <class T, class... V>
void d2_impl_seq(T const& t, V const&... v) {
  std::cerr << " " << t;
  d2_impl_seq(v...);
}

#define D0(x) do { std::cerr << __FILE__ ":" << __LINE__ << ":" << __func__ <<  " " << x << std::endl; } while (0)
// #ifdef DEBUG
#define D1(x...) do {                                         \
    std::cerr << __LINE__ << "  {" << #x << "} = {";  \
    d1_impl_seq(x);                                           \
    std::cerr << std::endl << std::flush;                     \
  } while(0)
// #else
// #define D1(x...) {}
// #endif
#define D2(x...) do {                     \
    std::cerr << "!";                     \
    d2_impl_seq(x);                       \
    std::cerr << std::endl << std::flush; \
  } while(0)

using namespace std;

static constexpr int INF = 1<<30;

// #define NDEBUG

static std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> elems;
  std::stringstream ss(s);
  std::string item;
  while (getline(ss, item, delim)) {
    if (!item.empty()) {
      elems.push_back(item);
    }
  }
  return elems;
}

struct XorShift {
  unsigned int x, y, z, w;
  double nL[65536];
  XorShift() { init(); }
  void init()
  {
    x = 314159265; y = 358979323; z = 846264338; w = 327950288;
    double n = 1 / (double)(2 * 65536);
    for (int i = 0; i < 65536; i++) {
      nL[i] = log(((double)i / 65536) + n);
    }
  }
  inline unsigned int next() { unsigned int t=x^x<<11; x=y; y=z; z=w; return w=w^w>>19^t^t>>8; }
  inline double nextLog() { return nL[next()&0xFFFF]; }
  inline int nextInt(int m) { return (int)(next()%m); } // [0, m)
  int nextInt(int min, int max) { return min+nextInt(max-min+1); } // [min, max]
  inline double nextDouble() { return (double)next()/((long long)1<<32); } // [0, 1]
  // 標準の乱数エンジンインターフェースに対応するoperator()
  using result_type = unsigned int;
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() { return 0xFFFFFFFF; }
  result_type operator()() {
    return next();
  }
};
XorShift rng;

template <typename T>
inline void rough_shuffle(vector<T>& lv) {
    int n = lv.size();
    for (int i = n; i > 0; --i) {
        int id = rng.nextInt(i);
        swap(lv[id], lv[i-1]);
    }
}

std::size_t calc_hash(std::vector<int> const& vec) {
  std::size_t seed = vec.size();
  for(auto& i : vec) {
    seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }
  return seed;
}

struct Timer {
  double LIMIT;
  Timer() : LIMIT(0.95) { reset(); }
  Timer(double limit) : LIMIT(limit) { reset(); }
	chrono::system_clock::time_point start;
	void reset() {
		start = chrono::system_clock::now();
	}
 
	double get() const {
		auto end = chrono::system_clock::now();
		return chrono::duration_cast<chrono::milliseconds>(end - start).count()/1000.0;
	}

  bool timeout() const {
    return get() >= LIMIT;
  }
};

#endif
